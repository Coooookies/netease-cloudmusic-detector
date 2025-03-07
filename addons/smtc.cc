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

// Standardized error handling
inline void ThrowJSException(Napi::Env env, const char *source, const std::exception &e)
{
  std::string message = std::string(source) + ": " + e.what();
  Napi::Error::New(env, message).ThrowAsJavaScriptException();
}

inline void ThrowJSException(Napi::Env env, const char *source, const winrt::hresult_error &e)
{
  std::string message = std::string(source) + ": " +
                        winrt::to_string(e.message()) +
                        " (HRESULT: 0x" +
                        std::to_string(static_cast<uint32_t>(e.code())) +
                        ")";
  Napi::Error::New(env, message).ThrowAsJavaScriptException();
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

  // Event callback management
  struct CallbackData
  {
    Napi::ThreadSafeFunction tsfn;
    bool active;
  };

  std::map<std::string, CallbackData> eventCallbacks;
  std::mutex callbacksLock;

  // Methods exposed to JavaScript
  Napi::Value GetSessions(const Napi::CallbackInfo &info);
  Napi::Value GetCurrentSession(const Napi::CallbackInfo &info);
  Napi::Value GetSessionInfo(const Napi::CallbackInfo &info);

  // Event listener methods
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

  // Helper methods for event handling
  bool RegisterEventListener(const std::string &eventName, Napi::Function callback, Napi::Env env);
  Napi::ThreadSafeFunction GetEventCallback(const std::string &eventName);
  void InvokeCallback(const std::string &eventName, const std::string &appId);
  bool Initialize(Napi::Env env);

  // 将 Cleanup 拆分为两个版本
  void Cleanup();              // 无参数版本，供析构函数使用
  void Cleanup(Napi::Env env); // 带环境参数的版本
};

Napi::FunctionReference SMTCMedia::constructor;

Napi::Object SMTCMedia::Init(Napi::Env env, Napi::Object exports)
{
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "SMTCMedia", {
                                                          // 注册类的实例方法
                                                          InstanceMethod("getSessions", &SMTCMedia::GetSessions),
                                                          InstanceMethod("getCurrentSession", &SMTCMedia::GetCurrentSession),
                                                          InstanceMethod("getSessionInfo", &SMTCMedia::GetSessionInfo),
                                                          InstanceMethod("on", &SMTCMedia::On),
                                                          InstanceMethod("off", &SMTCMedia::Off),
                                                      });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  // 导出
  exports.Set("SMTCMedia", func);
  return exports;
}

SMTCMedia::SMTCMedia(const Napi::CallbackInfo &info) : Napi::ObjectWrap<SMTCMedia>(info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  winrt::init_apartment();

  try
  {
    auto sessionManagerTask = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
    sessionManager = sessionManagerTask.get();

    if (!sessionManager)
    {
      Napi::Error::New(env, "Failed to initialize the Media Session Manager").ThrowAsJavaScriptException();
      return;
    }
  }
  catch (const std::exception &e)
  {
    ThrowJSException(env, "Constructor", e);
  }
  catch (const winrt::hresult_error &e)
  {
    ThrowJSException(env, "Constructor", e);
  }
}

void SMTCMedia::Cleanup()
{
  // Clean up all event callbacks
  {
    std::unique_lock<std::mutex> lock(callbacksLock);
    for (auto &pair : eventCallbacks)
    {
      if (pair.second.active && pair.second.tsfn)
      {
        auto tsfn = std::move(pair.second.tsfn);
        pair.second.active = false;

        lock.unlock();
        tsfn.Release();
        lock.lock();
      }
    }
    eventCallbacks.clear();
  }

  // Clean up event listeners
  UnregisterAllEvents();

  // Clean up WinRT
  if (sessionManager != nullptr)
  {
    sessionManager = nullptr;
  }
  winrt::uninit_apartment();
}

void SMTCMedia::Cleanup(Napi::Env env)
{
  // Clean up all event callbacks
  {
    std::unique_lock<std::mutex> lock(callbacksLock);
    for (auto &pair : eventCallbacks)
    {
      if (pair.second.active && pair.second.tsfn)
      {
        auto tsfn = std::move(pair.second.tsfn);
        pair.second.active = false;

        lock.unlock();
        tsfn.Release();
        lock.lock();
      }
    }
    eventCallbacks.clear();
  }

  // Clean up event listeners
  UnregisterAllEvents();

  // Clean up WinRT
  if (sessionManager != nullptr)
  {
    sessionManager = nullptr;
  }
  winrt::uninit_apartment();
}

SMTCMedia::~SMTCMedia()
{
  try
  {
    // 不传递 env 参数，修改 Cleanup 方法接受可选的 env 参数
    Cleanup(); // 删除 Napi::Env() 参数
  }
  catch (...)
  {
    // Destructor should not throw
  }
}

Napi::ThreadSafeFunction SMTCMedia::GetEventCallback(const std::string &eventName)
{
  std::lock_guard<std::mutex> lock(callbacksLock);
  auto it = eventCallbacks.find(eventName);
  if (it != eventCallbacks.end() && it->second.active)
  {
    return it->second.tsfn;
  }
  return Napi::ThreadSafeFunction();
}

void SMTCMedia::InvokeCallback(const std::string &eventName, const std::string &appId)
{
  auto tsfn = GetEventCallback(eventName);
  if (!tsfn)
    return;

  // Copy data for thread safety
  std::string eventNameCopy = eventName;
  std::string appIdCopy = appId;

  // Use NonBlockingCall to avoid blocking WinRT thread
  tsfn.NonBlockingCall(
      [eventNameCopy, appIdCopy](Napi::Env env, Napi::Function jsCallback)
      {
        // Execute in JavaScript thread
        Napi::HandleScope scope(env);
        jsCallback.Call({Napi::String::New(env, appIdCopy)});
      });
}

bool SMTCMedia::RegisterEventListener(const std::string &eventName, Napi::Function callback, Napi::Env env)
{
  std::unique_lock<std::mutex> lock(callbacksLock);

  // 释放现有回调（如果存在）
  auto it = eventCallbacks.find(eventName);
  if (it != eventCallbacks.end())
  {
    if (it->second.active && it->second.tsfn)
    {
      auto tsfn = std::move(it->second.tsfn);
      it->second.active = false;
      // 在外面释放
      lock.unlock();
      tsfn.Release();
      lock.lock();
    }
  }

  // 创建新的线程安全函数
  auto tsfn = Napi::ThreadSafeFunction::New(
      env,
      callback,
      "SMTC " + eventName,
      0,
      1,
      [](Napi::Env) {} // 空的终结器
  );

  // 存储回调数据
  eventCallbacks[eventName] = {std::move(tsfn), true};

  return true;
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

    // Release all callbacks
    {
      std::unique_lock<std::mutex> cbLock(callbacksLock);
      for (auto &pair : eventCallbacks)
      {
        if (pair.second.active && pair.second.tsfn)
        {
          auto tsfn = std::move(pair.second.tsfn);
          pair.second.active = false;
          // Release outside the lock
          cbLock.unlock();
          tsfn.Release();
          cbLock.lock();
        }
      }
      eventCallbacks.clear();
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
    // Register the event listener
    if (!RegisterEventListener(eventName, callback, env))
    {
      Napi::Error::New(env, "Failed to register event listener").ThrowAsJavaScriptException();
      return env.Undefined();
    }

    auto sessions = sessionManager.GetSessions();

    if (eventName == "sessionadded" || eventName == "sessionremoved")
    {
      // If we don't have any session change tokens yet, register for the event
      if (sessionAddedToken.value == 0 && sessionRemovedToken.value == 0)
      {
        // Store current session IDs for comparison
        std::vector<std::wstring> previousSessionIds;
        for (auto session : sessions)
        {
          std::wstring appId = hstring_to_wstring(session.SourceAppUserModelId());
          previousSessionIds.push_back(appId);

          // Register events for this session
          RegisterSessionEvents(session);
        }

        // Register for session changes with proper thread safety
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
                
                // Notify JavaScript about added session using the safe invoke method
                InvokeCallback("sessionadded", hstring_to_string(id));
              }
            }
            
            // Find removed sessions
            for (auto& prevId : previousSessionIds) {
              // If this previous ID is not in the current list, it's been removed
              if (std::find(currentSessionIds.begin(), currentSessionIds.end(), prevId) == currentSessionIds.end()) {
                // Notify JavaScript about removed session using the safe invoke method
                InvokeCallback("sessionremoved", wstring_to_string(prevId));
                
                // Unregister events for this session
                UnregisterSessionEvents(prevId);
              }
            }
            
            // Update previous session IDs for next comparison
            previousSessionIds = currentSessionIds;
          } catch (...) {
            // Silently catch any exceptions to prevent crashes
          } });

        // Store the token for both event types
        sessionAddedToken = token;
        sessionRemovedToken = token;
      }
    }
    else if (eventName == "playbackstatechanged" ||
             eventName == "timelinepropertieschanged" ||
             eventName == "mediapropertieschanged")
    {
      // Register for events on all current sessions
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
    ThrowJSException(env, "On", e);
  }
  catch (const winrt::hresult_error &e)
  {
    ThrowJSException(env, "On", e);
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
    // 使用 unique_lock 替代 lock_guard，这样可以临时解锁
    std::unique_lock<std::mutex> lock(callbacksLock);
    auto it = eventCallbacks.find(eventName);
    if (it != eventCallbacks.end() && it->second.active)
    {
      auto tsfn = std::move(it->second.tsfn);
      it->second.active = false;
      eventCallbacks.erase(it);

      // 临时解锁，然后调用可能阻塞的方法
      lock.unlock();
      tsfn.Release();
      lock.lock();
    }

    // 处理特殊的会话事件...
    if (eventName == "sessionadded" || eventName == "sessionremoved")
    {
      bool hasSessionAddedListener = false;
      bool hasSessionRemovedListener = false;

      // 检查是否还有这些事件的监听器
      for (const auto &pair : eventCallbacks)
      {
        if (pair.first == "sessionadded" && pair.second.active)
        {
          hasSessionAddedListener = true;
        }
        else if (pair.first == "sessionremoved" && pair.second.active)
        {
          hasSessionRemovedListener = true;
        }
      }

      // 如果两种事件都没有监听器，注销令牌
      if (!hasSessionAddedListener && !hasSessionRemovedListener)
      {
        if (sessionAddedToken.value != 0)
        {
          // 临时解锁后调用 WinRT API
          lock.unlock();
          sessionManager.SessionsChanged(sessionAddedToken);
          lock.lock();
          sessionAddedToken = {};
        }

        if (sessionRemovedToken.value != 0)
        {
          // 临时解锁后调用 WinRT API
          lock.unlock();
          sessionManager.SessionsChanged(sessionRemovedToken);
          lock.lock();
          sessionRemovedToken = {};
        }
      }
    }

    // 检查是否需要注销任何事件处理程序
    bool hasAnyEventListeners = false;
    for (const auto &pair : eventCallbacks)
    {
      if (pair.second.active)
      {
        hasAnyEventListeners = true;
        break;
      }
    }

    // 如果没有更多事件监听器，注销所有事件处理程序
    if (!hasAnyEventListeners)
    {
      lock.unlock();
      UnregisterAllEvents();
    }
  }
  catch (const std::exception &e)
  {
    ThrowJSException(env, "Off", e);
  }
  catch (const winrt::hresult_error &e)
  {
    ThrowJSException(env, "Off", e);
  }

  return env.Undefined();
}

void SMTCMedia::RegisterSessionEvents(GlobalSystemMediaTransportControlsSession const &session)
{
  try
  {
    auto id = session.SourceAppUserModelId();
    std::wstring appId = hstring_to_wstring(id);
    std::string appIdStr = hstring_to_string(id);

    std::lock_guard<std::mutex> lock(sessionsLock);

    // Remove any existing tokens for this session
    auto existingTokens = sessionEventTokens.find(appId);
    if (existingTokens != sessionEventTokens.end())
    {
      // Try to cleanly unregister existing event handlers
      try
      {
        auto sessions = sessionManager.GetSessions();
        for (auto existingSession : sessions)
        {
          if (hstring_to_wstring(existingSession.SourceAppUserModelId()) == appId)
          {
            // Try to unregister each token
            for (auto &token : existingTokens->second)
            {
              // Try each event type
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

      // Remove the tokens
      sessionEventTokens.erase(existingTokens);
    }

    std::vector<event_token> tokens;

    // Register for playback info changed with proper thread-safety
    auto playbackToken = session.PlaybackInfoChanged(
        [this, appIdStr](auto &&, auto &&)
        {
          InvokeCallback("playbackstatechanged", appIdStr);
        });
    tokens.push_back(playbackToken);

    // Register for timeline properties changed with proper thread-safety
    auto timelineToken = session.TimelinePropertiesChanged(
        [this, appIdStr](auto &&, auto &&)
        {
          InvokeCallback("timelinepropertieschanged", appIdStr);
        });
    tokens.push_back(timelineToken);

    // Register for media properties changed with proper thread-safety
    auto mediaToken = session.MediaPropertiesChanged(
        [this, appIdStr](auto &&, auto &&)
        {
          InvokeCallback("mediapropertieschanged", appIdStr);
        });
    tokens.push_back(mediaToken);

    // Store the tokens
    sessionEventTokens[appId] = std::move(tokens);
  }
  catch (...)
  {
    // Silently catch exceptions
  }
}

void SMTCMedia::UnregisterSessionEvents(std::wstring const &sourceAppUserModelId)
{
  std::lock_guard<std::mutex> lock(sessionsLock);
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
      // Register events for all sessions
      RegisterSessionEvents(session);
      result.Set(i++, CreateSessionObject(env, session));
    }

    return result;
  }
  catch (const std::exception &e)
  {
    ThrowJSException(env, "GetSessions", e);
    return env.Null();
  }
  catch (const winrt::hresult_error &e)
  {
    ThrowJSException(env, "GetSessions", e);
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
    ThrowJSException(env, "GetCurrentSession", e);
    return env.Null();
  }
  catch (const winrt::hresult_error &e)
  {
    ThrowJSException(env, "GetCurrentSession", e);
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
    ThrowJSException(env, "GetSessionInfo", e);
    return env.Null();
  }
  catch (const winrt::hresult_error &e)
  {
    ThrowJSException(env, "GetSessionInfo", e);
    return env.Null();
  }
}

// This method uses N-API's recommended approach for creating JS objects
Napi::Object SMTCMedia::CreateSessionObject(Napi::Env env, GlobalSystemMediaTransportControlsSession const &session)
{
  Napi::Object obj = Napi::Object::New(env);

  try
  {
    // Basic session info
    obj.Set("sourceAppUserModelId", Napi::String::New(env, hstring_to_string(session.SourceAppUserModelId())));

    // Use AsyncWork for the media properties in a real implementation
    // For now, synchronously get properties but with better error handling
    try
    {
      auto mediaPropertiesTask = session.TryGetMediaPropertiesAsync();
      auto mediaProperties = mediaPropertiesTask.get();
      if (mediaProperties)
      {
        obj.Set("mediaProperties", CreateMediaPropertiesObject(env, mediaProperties));
      }
    }
    catch (const std::exception &e)
    {
      obj.Set("mediaPropertiesError", Napi::String::New(env, e.what()));
    }
    catch (const winrt::hresult_error &e)
    {
      obj.Set("mediaPropertiesError", Napi::String::New(env, winrt::to_string(e.message())));
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