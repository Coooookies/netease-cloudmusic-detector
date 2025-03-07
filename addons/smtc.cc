#include <napi.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

// Helper functions for string conversion
inline std::wstring hstring_to_wstring(const winrt::hstring &hstring)
{
  return std::wstring(hstring.c_str());
}

inline std::string hstring_to_string(const winrt::hstring &hstring)
{
  return winrt::to_string(hstring);
}

// Convert std::wstring to std::string for use with JavaScript
inline std::string wstring_to_string(const std::wstring &wstr)
{
  std::string str;
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
  str.resize(size_needed);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
  return str;
}

class SMTCMedia : public Napi::ObjectWrap<SMTCMedia>
{
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  SMTCMedia(const Napi::CallbackInfo &info);
  ~SMTCMedia();

private:
  static Napi::FunctionReference constructor;

  // SMTC session manager
  GlobalSystemMediaTransportControlsSessionManager sessionManager{nullptr};

  // Event tokens for session manager and sessions
  event_token sessionAddedToken{};
  event_token sessionRemovedToken{};
  std::mutex sessionsLock;
  std::map<std::wstring, std::vector<event_token>> sessionEventTokens;

  // Callback references
  Napi::ThreadSafeFunction tsfnSessionAdded;
  Napi::ThreadSafeFunction tsfnSessionRemoved;
  Napi::ThreadSafeFunction tsfnPlaybackStateChanged;
  Napi::ThreadSafeFunction tsfnTimelinePropertiesChanged;
  Napi::ThreadSafeFunction tsfnMediaPropertiesChanged;

  // Methods exposed to JavaScript
  Napi::Value GetSessions(const Napi::CallbackInfo &info);
  Napi::Value GetCurrentSession(const Napi::CallbackInfo &info);
  Napi::Value GetSessionInfo(const Napi::CallbackInfo &info);

  // New event listener methods
  Napi::Value On(const Napi::CallbackInfo &info);
  Napi::Value Off(const Napi::CallbackInfo &info);

  // Helper methods
  Napi::Object CreateSessionObject(Napi::Env env, GlobalSystemMediaTransportControlsSession const &session);
  Napi::Object CreateMediaPropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionMediaProperties const &properties);
  Napi::Object CreateTimelinePropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionTimelineProperties const &properties);
  Napi::Object CreatePlaybackInfoObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionPlaybackInfo const &playbackInfo);

  // Event handler methods
  void RegisterSessionEvents(GlobalSystemMediaTransportControlsSession const &session);
  void UnregisterSessionEvents(std::wstring const &sourceAppUserModelId);
  void UnregisterAllEvents();
};

Napi::FunctionReference SMTCMedia::constructor;

Napi::Object SMTCMedia::Init(Napi::Env env, Napi::Object exports)
{
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "SMTCMedia", {
                                                          InstanceMethod("getSessions", &SMTCMedia::GetSessions),
                                                          InstanceMethod("getCurrentSession", &SMTCMedia::GetCurrentSession),
                                                          InstanceMethod("getSessionInfo", &SMTCMedia::GetSessionInfo),
                                                          InstanceMethod("on", &SMTCMedia::On),
                                                          InstanceMethod("off", &SMTCMedia::Off),
                                                      });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("SMTCMedia", func);
  return exports;
}

SMTCMedia::SMTCMedia(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<SMTCMedia>(info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try
  {
    // Initialize WinRT
    winrt::init_apartment();

    // Get the session manager
    sessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
  }
  catch (const std::exception &e)
  {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (const winrt::hresult_error &e)
  {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
  }
}

SMTCMedia::~SMTCMedia()
{
  // Clean up event listeners
  UnregisterAllEvents();

  // Clean up WinRT
  if (sessionManager != nullptr)
  {
    sessionManager = nullptr;
  }
  winrt::uninit_apartment();
}

void SMTCMedia::UnregisterAllEvents()
{
  try
  {
    // Unregister session manager events
    if (sessionManager != nullptr)
    {
      if (sessionAddedToken.value != 0)
      {
        sessionManager.SessionsChanged(sessionAddedToken);
        sessionAddedToken = {};
      }

      if (sessionRemovedToken.value != 0)
      {
        sessionManager.SessionsChanged(sessionRemovedToken);
        sessionRemovedToken = {};
      }
    }

    // Unregister session events
    std::lock_guard<std::mutex> lock(sessionsLock);
    sessionEventTokens.clear();

    // Release thread-safe function references
    if (tsfnSessionAdded)
    {
      auto tsfn = std::move(tsfnSessionAdded);
      tsfn.Release();
    }

    if (tsfnSessionRemoved)
    {
      auto tsfn = std::move(tsfnSessionRemoved);
      tsfn.Release();
    }

    if (tsfnPlaybackStateChanged)
    {
      auto tsfn = std::move(tsfnPlaybackStateChanged);
      tsfn.Release();
    }

    if (tsfnTimelinePropertiesChanged)
    {
      auto tsfn = std::move(tsfnTimelinePropertiesChanged);
      tsfn.Release();
    }

    if (tsfnMediaPropertiesChanged)
    {
      auto tsfn = std::move(tsfnMediaPropertiesChanged);
      tsfn.Release();
    }
  }
  catch (...)
  {
    // Silently catch any exceptions to prevent crashes during cleanup
  }
}

Napi::Value SMTCMedia::On(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction())
  {
    Napi::TypeError::New(env, "Expected event name and callback function").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string eventName = info[0].As<Napi::String>().Utf8Value();
  Napi::Function callback = info[1].As<Napi::Function>();

  try
  {
    // Create thread-safe function with proper finalization
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "SMTC Callback",
        0,
        1,
        [](Napi::Env) {} // Empty finalizer
    );

    auto sessions = sessionManager.GetSessions();

    if (eventName == "sessionadded" || eventName == "sessionremoved")
    {
      // For both session added and removed events, we use the same SessionsChanged event
      // but handle them differently in the callback

      // Release previous thread-safe functions if they exist
      if (eventName == "sessionadded")
      {
        if (tsfnSessionAdded)
          tsfnSessionAdded.Release();
        tsfnSessionAdded = std::move(tsfn);
      }
      else
      {
        if (tsfnSessionRemoved)
          tsfnSessionRemoved.Release();
        tsfnSessionRemoved = std::move(tsfn);
      }

      // If we don't have any session change tokens yet, register for the event
      if (sessionAddedToken.value == 0 && sessionRemovedToken.value == 0)
      {
        // Store current session IDs for comparison
        std::vector<std::wstring> previousSessionIds;
        for (auto session : sessions)
        {
          std::wstring appId = hstring_to_wstring(session.SourceAppUserModelId());
          previousSessionIds.push_back(appId);

          // Register events for this session - make sure we register all possible events
          // regardless of whether there's a listener or not
          RegisterSessionEvents(session);
        }

        // Register for session changes
        auto token = sessionManager.SessionsChanged([this, previousSessionIds](auto &&sender, auto &&args) mutable
                                                    {
          // Make a local copy of the session manager to avoid race conditions
          auto sessionMgr = this->sessionManager;
          if (!sessionMgr) return;
          
          try {
            // Get current sessions
            auto sessions = sessionMgr.GetSessions();
            std::vector<std::wstring> currentSessionIds;
            
            // Get all current session IDs
            for (auto session : sessions) {
              currentSessionIds.push_back(hstring_to_wstring(session.SourceAppUserModelId()));
            }
            
            // Find new sessions (added)
            for (auto session : sessions) {
              auto id = session.SourceAppUserModelId();
              std::wstring appId(hstring_to_wstring(id));
              
              // If this session ID wasn't in the previous list, it's new
              if (std::find(previousSessionIds.begin(), previousSessionIds.end(), appId) == previousSessionIds.end()) {
                // Register events for this session
                
                RegisterSessionEvents(session);
                
                // Notify JavaScript about added session
                if (tsfnSessionAdded) {
                  std::string idCopy = hstring_to_string(id);
                  tsfnSessionAdded.BlockingCall([idCopy](Napi::Env env, Napi::Function jsCallback) {
                    Napi::HandleScope scope(env);
                    jsCallback.Call({Napi::String::New(env, idCopy)});
                  });
                }
              }
            }
            
            // Find removed sessions
            for (auto& prevId : previousSessionIds) {
              // If this previous ID is not in the current list, it's been removed
              if (std::find(currentSessionIds.begin(), currentSessionIds.end(), prevId) == currentSessionIds.end()) {
                // Notify JavaScript about removed session
                if (tsfnSessionRemoved) {
                  std::string idCopy = wstring_to_string(prevId);
                  tsfnSessionRemoved.BlockingCall([idCopy](Napi::Env env, Napi::Function jsCallback) {
                    Napi::HandleScope scope(env);
                    jsCallback.Call({Napi::String::New(env, idCopy)});
                  });
                }
                
                // Unregister events for this session
                UnregisterSessionEvents(prevId);
              }
            }
            
            // Update previous session IDs for next comparison
            previousSessionIds = currentSessionIds;
          } catch (...) {
            // Silently catch any exceptions to prevent crashes
          } });

        // Store the token - we'll use the same token for both added and removed events
        sessionAddedToken = token;
        sessionRemovedToken = token;
      }
    }
    else if (eventName == "playbackstatechanged")
    {
      // Release previous thread-safe function if it exists
      if (tsfnPlaybackStateChanged)
        tsfnPlaybackStateChanged.Release();

      // Store thread-safe function
      tsfnPlaybackStateChanged = std::move(tsfn);

      // Register for playback state changes on all current sessions
      for (auto session : sessions)
      {
        RegisterSessionEvents(session);
      }
    }
    else if (eventName == "timelinepropertieschanged")
    {
      // Release previous thread-safe function if it exists
      if (tsfnTimelinePropertiesChanged)
        tsfnTimelinePropertiesChanged.Release();

      // Store thread-safe function
      tsfnTimelinePropertiesChanged = std::move(tsfn);

      // Register for timeline properties changes on all current sessions
      for (auto session : sessions)
      {
        RegisterSessionEvents(session);
      }
    }
    else if (eventName == "mediapropertieschanged")
    {
      // Release previous thread-safe function if it exists
      if (tsfnMediaPropertiesChanged)
        tsfnMediaPropertiesChanged.Release();

      // Store thread-safe function
      tsfnMediaPropertiesChanged = std::move(tsfn);

      // Register for media properties changes on all current sessions
      for (auto session : sessions)
      {
        RegisterSessionEvents(session);
      }
    }
    else
    {
      Napi::Error::New(env, "Unknown event: " + eventName).ThrowAsJavaScriptException();
      return env.Undefined();
    }
  }
  catch (const std::exception &e)
  {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (const winrt::hresult_error &e)
  {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
  }

  return env.Undefined();
}

Napi::Value SMTCMedia::Off(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsString())
  {
    Napi::TypeError::New(env, "Expected event name").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string eventName = info[0].As<Napi::String>().Utf8Value();

  try
  {
    if (eventName == "sessionadded")
    {
      // Release the thread-safe function
      if (tsfnSessionAdded)
      {
        auto tsfn = std::move(tsfnSessionAdded);
        tsfn.Release();
      }

      // Only unregister the token if sessionremoved is also not being used
      if (sessionAddedToken.value != 0 && !tsfnSessionRemoved)
      {
        sessionManager.SessionsChanged(sessionAddedToken);
        sessionAddedToken = {};
      }
    }
    else if (eventName == "sessionremoved")
    {
      // Release the thread-safe function
      if (tsfnSessionRemoved)
      {
        auto tsfn = std::move(tsfnSessionRemoved);
        tsfn.Release();
      }

      // Only unregister the token if sessionadded is also not being used
      if (sessionRemovedToken.value != 0 && !tsfnSessionAdded)
      {
        sessionManager.SessionsChanged(sessionRemovedToken);
        sessionRemovedToken = {};
      }
    }
    else if (eventName == "playbackstatechanged")
    {
      if (tsfnPlaybackStateChanged)
      {
        auto tsfn = std::move(tsfnPlaybackStateChanged);
        tsfn.Release();
      }
    }
    else if (eventName == "timelinepropertieschanged")
    {
      if (tsfnTimelinePropertiesChanged)
      {
        auto tsfn = std::move(tsfnTimelinePropertiesChanged);
        tsfn.Release();
      }
    }
    else if (eventName == "mediapropertieschanged")
    {
      if (tsfnMediaPropertiesChanged)
      {
        auto tsfn = std::move(tsfnMediaPropertiesChanged);
        tsfn.Release();
      }
    }
    else
    {
      Napi::Error::New(env, "Unknown event: " + eventName).ThrowAsJavaScriptException();
    }
  }
  catch (const std::exception &e)
  {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (const winrt::hresult_error &e)
  {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
  }

  return env.Undefined();
}

void SMTCMedia::RegisterSessionEvents(GlobalSystemMediaTransportControlsSession const &session)
{
  try
  {
    auto id = session.SourceAppUserModelId();
    std::wstring appId = hstring_to_wstring(id);
    std::string appIdStr = hstring_to_string(id); // Convert to std::string for JavaScript

    std::lock_guard<std::mutex> lock(sessionsLock);

    // Remove any existing tokens for this session
    auto existingTokens = sessionEventTokens.find(appId);
    if (existingTokens != sessionEventTokens.end())
    {
      // Attempt to unregister existing tokens
      try
      {
        // We need to find the session with this ID
        auto sessions = sessionManager.GetSessions();
        for (auto existingSession : sessions)
        {
          if (hstring_to_wstring(existingSession.SourceAppUserModelId()) == appId)
          {
            // Unregister the events by their tokens
            for (auto &token : existingTokens->second)
            {
              // We don't know which event this token belongs to, so we'll try all of them
              try
              {
                existingSession.PlaybackInfoChanged(token);
              }
              catch (...)
              {
              }

              try
              {
                existingSession.TimelinePropertiesChanged(token);
              }
              catch (...)
              {
              }

              try
              {
                existingSession.MediaPropertiesChanged(token);
              }
              catch (...)
              {
              }
            }
            break;
          }
        }
      }
      catch (...)
      {
      }

      // Remove the tokens from our map
      sessionEventTokens.erase(existingTokens);
    }

    std::vector<event_token> tokens;

    // Always register for all event types, regardless of whether there's a listener or not
    // This ensures events are already registered when a listener is added later

    // Register for playback info changed
    {
      std::string appIdCopy = appIdStr;
      auto token = session.PlaybackInfoChanged([this, appIdCopy](auto &&session, auto &&args)
                                               {
        try {
          if (tsfnPlaybackStateChanged) {
            auto tsfn = tsfnPlaybackStateChanged;
            if (tsfn) {
              tsfn.BlockingCall([appIdCopy](Napi::Env env, Napi::Function jsCallback) {
                Napi::HandleScope scope(env);
                jsCallback.Call({Napi::String::New(env, appIdCopy)});
              });
            }
          }
        } catch (...) {} });
      tokens.push_back(token);
    }

    // Register for timeline properties changed
    {
      std::string appIdCopy = appIdStr;
      auto token = session.TimelinePropertiesChanged([this, appIdCopy](auto &&session, auto &&args)
                                                     {
        try {
          if (tsfnTimelinePropertiesChanged) {
            auto tsfn = tsfnTimelinePropertiesChanged;
            if (tsfn) {
              tsfn.BlockingCall([appIdCopy](Napi::Env env, Napi::Function jsCallback) {
                Napi::HandleScope scope(env);
                jsCallback.Call({Napi::String::New(env, appIdCopy)});
              });
            }
          }
        } catch (...) {} });
      tokens.push_back(token);
    }

    // Register for media properties changed
    {
      std::string appIdCopy = appIdStr;
      auto token = session.MediaPropertiesChanged([this, appIdCopy](auto &&session, auto &&args)
                                                  {
        try {
          if (tsfnMediaPropertiesChanged) {
            auto tsfn = tsfnMediaPropertiesChanged;
            if (tsfn) {
              tsfn.BlockingCall([appIdCopy](Napi::Env env, Napi::Function jsCallback) {
                Napi::HandleScope scope(env);
                jsCallback.Call({Napi::String::New(env, appIdCopy)});
              });
            }
          }
        } catch (...) {} });
      tokens.push_back(token);
    }

    // Store the tokens
    sessionEventTokens[appId] = std::move(tokens);
  }
  catch (...)
  {
    // Silently catch any exceptions to prevent crashes
  }
}

void SMTCMedia::UnregisterSessionEvents(std::wstring const &sourceAppUserModelId)
{
  std::lock_guard<std::mutex> lock(sessionsLock);

  // Remove from the map
  sessionEventTokens.erase(sourceAppUserModelId);
}

Napi::Value SMTCMedia::GetSessions(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try
  {
    auto sessions = sessionManager.GetSessions();
    Napi::Array result = Napi::Array::New(env, sessions.Size());

    uint32_t i = 0;
    for (auto session : sessions)
    {
      // 确保为所有会话注册事件
      RegisterSessionEvents(session);
      result.Set(i++, CreateSessionObject(env, session));
    }

    return result;
  }
  catch (const std::exception &e)
  {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
  catch (const winrt::hresult_error &e)
  {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Value SMTCMedia::GetCurrentSession(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try
  {
    auto currentSession = sessionManager.GetCurrentSession();
    if (currentSession)
    {
      return CreateSessionObject(env, currentSession);
    }
    return env.Null();
  }
  catch (const std::exception &e)
  {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
  catch (const winrt::hresult_error &e)
  {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Value SMTCMedia::GetSessionInfo(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try
  {
    if (info.Length() < 1 || !info[0].IsString())
    {
      Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
      return env.Null();
    }

    std::string sessionId = info[0].As<Napi::String>().Utf8Value();
    auto sessions = sessionManager.GetSessions();

    for (auto session : sessions)
    {
      if (hstring_to_string(session.SourceAppUserModelId()) == sessionId)
      {
        return CreateSessionObject(env, session);
      }
    }

    return env.Null();
  }
  catch (const std::exception &e)
  {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
  catch (const winrt::hresult_error &e)
  {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Object SMTCMedia::CreateSessionObject(Napi::Env env, GlobalSystemMediaTransportControlsSession const &session)
{
  Napi::Object obj = Napi::Object::New(env);

  try
  {
    // Basic session info
    obj.Set("sourceAppUserModelId", Napi::String::New(env, hstring_to_string(session.SourceAppUserModelId())));

    // Media properties
    auto mediaPropertiesTask = session.TryGetMediaPropertiesAsync();
    auto mediaProperties = mediaPropertiesTask.get();
    if (mediaProperties)
    {
      obj.Set("mediaProperties", CreateMediaPropertiesObject(env, mediaProperties));
    }

    // Timeline properties
    auto timelineProperties = session.GetTimelineProperties();
    obj.Set("timelineProperties", CreateTimelinePropertiesObject(env, timelineProperties));

    // Playback info
    auto playbackInfo = session.GetPlaybackInfo();
    obj.Set("playbackInfo", CreatePlaybackInfoObject(env, playbackInfo));
  }
  catch (const std::exception &e)
  {
    // Just log the error but continue
    obj.Set("error", Napi::String::New(env, e.what()));
  }
  catch (const winrt::hresult_error &e)
  {
    // Just log the error but continue
    obj.Set("error", Napi::String::New(env, winrt::to_string(e.message())));
  }

  return obj;
}

Napi::Object SMTCMedia::CreateMediaPropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionMediaProperties const &properties)
{
  Napi::Object obj = Napi::Object::New(env);

  obj.Set("title", Napi::String::New(env, hstring_to_string(properties.Title())));
  obj.Set("artist", Napi::String::New(env, hstring_to_string(properties.Artist())));
  obj.Set("albumTitle", Napi::String::New(env, hstring_to_string(properties.AlbumTitle())));
  obj.Set("albumArtist", Napi::String::New(env, hstring_to_string(properties.AlbumArtist())));
  obj.Set("trackNumber", Napi::Number::New(env, properties.TrackNumber()));

  // Handle genres collection safely
  auto genres = properties.Genres();
  if (genres.Size() > 0)
  {
    obj.Set("genres", Napi::String::New(env, hstring_to_string(genres.GetAt(0))));
  }
  else
  {
    obj.Set("genres", Napi::String::New(env, ""));
  }

  // Handle playback type safely
  auto playbackType = properties.PlaybackType();
  if (playbackType)
  {
    obj.Set("playbackType", Napi::Number::New(env, static_cast<int>(playbackType.Value())));
  }
  else
  {
    obj.Set("playbackType", Napi::Number::New(env, -1));
  }

  return obj;
}

Napi::Object SMTCMedia::CreateTimelinePropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionTimelineProperties const &properties)
{
  Napi::Object obj = Napi::Object::New(env);

  auto startTime = properties.StartTime();
  auto endTime = properties.EndTime();
  auto position = properties.Position();

  obj.Set("startTimeInSeconds", Napi::Number::New(env, startTime.count() / 10000000.0));
  obj.Set("endTimeInSeconds", Napi::Number::New(env, endTime.count() / 10000000.0));
  obj.Set("positionInSeconds", Napi::Number::New(env, position.count() / 10000000.0));
  obj.Set("minSeekTimeInSeconds", Napi::Number::New(env, properties.MinSeekTime().count() / 10000000.0));
  obj.Set("maxSeekTimeInSeconds", Napi::Number::New(env, properties.MaxSeekTime().count() / 10000000.0));

  return obj;
}

Napi::Object SMTCMedia::CreatePlaybackInfoObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionPlaybackInfo const &playbackInfo)
{
  Napi::Object obj = Napi::Object::New(env);

  obj.Set("playbackStatus", Napi::Number::New(env, static_cast<int>(playbackInfo.PlaybackStatus())));

  // Handle playback type safely
  auto playbackType = playbackInfo.PlaybackType();
  if (playbackType)
  {
    obj.Set("playbackType", Napi::Number::New(env, static_cast<int>(playbackType.Value())));
  }
  else
  {
    obj.Set("playbackType", Napi::Number::New(env, -1));
  }

  // Handle shuffle active safely
  auto isShuffleActive = playbackInfo.IsShuffleActive();
  if (isShuffleActive)
  {
    obj.Set("isShuffleActive", Napi::Boolean::New(env, isShuffleActive.Value()));
  }
  else
  {
    obj.Set("isShuffleActive", Napi::Boolean::New(env, false));
  }

  // Handle auto repeat mode safely
  auto autoRepeatMode = playbackInfo.AutoRepeatMode();
  if (autoRepeatMode)
  {
    obj.Set("autoRepeatMode", Napi::Number::New(env, static_cast<int>(autoRepeatMode.Value())));
  }
  else
  {
    obj.Set("autoRepeatMode", Napi::Number::New(env, -1));
  }

  // Handle controls safely
  auto controls = playbackInfo.Controls();
  int controlsValue = 0;
  if (controls.IsPlayEnabled())
    controlsValue |= 1;
  if (controls.IsPauseEnabled())
    controlsValue |= 2;
  if (controls.IsStopEnabled())
    controlsValue |= 4;
  if (controls.IsNextEnabled())
    controlsValue |= 8;
  if (controls.IsPreviousEnabled())
    controlsValue |= 16;

  obj.Set("controls", Napi::Number::New(env, controlsValue));

  return obj;
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports)
{
  return SMTCMedia::Init(env, exports);
}

NODE_API_MODULE(smtc, InitAll)