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
inline std::wstring hstring_to_wstring(const winrt::hstring& hstring) {
    return std::wstring(hstring.c_str());
}

inline std::string hstring_to_string(const winrt::hstring& hstring) {
    return winrt::to_string(hstring);
}

// Convert std::wstring to std::string for use with JavaScript
inline std::string wstring_to_string(const std::wstring& wstr) {
    std::string str;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    str.resize(size_needed);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

class SMTCMedia : public Napi::ObjectWrap<SMTCMedia> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  SMTCMedia(const Napi::CallbackInfo& info);
  ~SMTCMedia();

private:
  static Napi::FunctionReference constructor;
  
  // SMTC session manager
  GlobalSystemMediaTransportControlsSessionManager sessionManager{ nullptr };
  
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
  Napi::Value GetSessions(const Napi::CallbackInfo& info);
  Napi::Value GetCurrentSession(const Napi::CallbackInfo& info);
  Napi::Value GetSessionInfo(const Napi::CallbackInfo& info);
  
  // New event listener methods
  Napi::Value On(const Napi::CallbackInfo& info);
  Napi::Value Off(const Napi::CallbackInfo& info);
  
  // Helper methods
  Napi::Object CreateSessionObject(Napi::Env env, GlobalSystemMediaTransportControlsSession const& session);
  Napi::Object CreateMediaPropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionMediaProperties const& properties);
  Napi::Object CreateTimelinePropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionTimelineProperties const& properties);
  Napi::Object CreatePlaybackInfoObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionPlaybackInfo const& playbackInfo);
  
  // Event handler methods
  void RegisterSessionEvents(GlobalSystemMediaTransportControlsSession const& session);
  void UnregisterSessionEvents(std::wstring const& sourceAppUserModelId);
  void UnregisterAllEvents();
};

Napi::FunctionReference SMTCMedia::constructor;

Napi::Object SMTCMedia::Init(Napi::Env env, Napi::Object exports) {
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

SMTCMedia::SMTCMedia(const Napi::CallbackInfo& info) 
  : Napi::ObjectWrap<SMTCMedia>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try {
    // Initialize WinRT
    winrt::init_apartment();
    
    // Get the session manager
    sessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
  }
  catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (const winrt::hresult_error& e) {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
  }
}

SMTCMedia::~SMTCMedia() {
  // Clean up event listeners
  UnregisterAllEvents();
  
  // Clean up WinRT
  if (sessionManager != nullptr) {
    sessionManager = nullptr;
  }
  winrt::uninit_apartment();
}

void SMTCMedia::UnregisterAllEvents() {
  // Unregister session manager events
  if (sessionManager != nullptr) {
    if (sessionAddedToken.value != 0) {
      sessionManager.SessionsChanged(sessionAddedToken);
      sessionAddedToken = {};
    }
    
    if (sessionRemovedToken.value != 0) {
      sessionManager.SessionsChanged(sessionRemovedToken);
      sessionRemovedToken = {};
    }
  }
  
  // Unregister session events
  std::lock_guard<std::mutex> lock(sessionsLock);
  sessionEventTokens.clear();
  
  // Release thread-safe function references
  if (tsfnSessionAdded) tsfnSessionAdded.Release();
  if (tsfnSessionRemoved) tsfnSessionRemoved.Release();
  if (tsfnPlaybackStateChanged) tsfnPlaybackStateChanged.Release();
  if (tsfnTimelinePropertiesChanged) tsfnTimelinePropertiesChanged.Release();
  if (tsfnMediaPropertiesChanged) tsfnMediaPropertiesChanged.Release();
}

Napi::Value SMTCMedia::On(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction()) {
    Napi::TypeError::New(env, "Expected event name and callback function").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  
  std::string eventName = info[0].As<Napi::String>().Utf8Value();
  Napi::Function callback = info[1].As<Napi::Function>();
  
  try {
    // Create thread-safe function
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
      env,
      callback,
      "SMTC Callback",
      0,
      1,
      [](Napi::Env) {}
    );
    
    if (eventName == "sessionadded") {
      // Store thread-safe function
      tsfnSessionAdded = std::move(tsfn);
      
      // Register for session added events
      sessionAddedToken = sessionManager.SessionsChanged([this](auto&& sender, auto&& args) {
        auto sessions = sessionManager.GetSessions();
        std::vector<std::wstring> currentSessionIds;
        
        // Get all current session IDs
        for (auto session : sessions) {
          currentSessionIds.push_back(hstring_to_wstring(session.SourceAppUserModelId()));
        }
        
        // Find new sessions
        for (auto session : sessions) {
          auto id = session.SourceAppUserModelId();
          std::wstring appId(hstring_to_wstring(id));
          
          std::lock_guard<std::mutex> lock(sessionsLock);
          if (sessionEventTokens.find(appId) == sessionEventTokens.end()) {
            // New session found, register for its events
            RegisterSessionEvents(session);
            
            // Notify JavaScript
            if (tsfnSessionAdded) {
              tsfnSessionAdded.BlockingCall([id](Napi::Env env, Napi::Function jsCallback) {
                jsCallback.Call({Napi::String::New(env, hstring_to_string(id))});
              });
            }
          }
        }
      });
      
      // Register events for existing sessions
      auto sessions = sessionManager.GetSessions();
      for (auto session : sessions) {
        RegisterSessionEvents(session);
      }
    }
    else if (eventName == "sessionremoved") {
      // Store thread-safe function
      tsfnSessionRemoved = std::move(tsfn);
      
      // Register for session removed events
      sessionRemovedToken = sessionManager.SessionsChanged([this](auto&& sender, auto&& args) {
        auto sessions = sessionManager.GetSessions();
        std::vector<std::wstring> currentSessionIds;
        
        // Get current session IDs
        for (auto session : sessions) {
          currentSessionIds.push_back(hstring_to_wstring(session.SourceAppUserModelId()));
        }
        
        // Check for removed sessions
        std::vector<std::wstring> removedSessions;
        {
          std::lock_guard<std::mutex> lock(sessionsLock);
          for (auto& [sessionId, tokens] : sessionEventTokens) {
            // If the session is not in the current list, it's been removed
            if (std::find(currentSessionIds.begin(), currentSessionIds.end(), sessionId) == currentSessionIds.end()) {
              removedSessions.push_back(sessionId);
            }
          }
          
          // Unregister removed sessions and clean up tokens
          for (auto& removedId : removedSessions) {
            // Notify JavaScript
            if (tsfnSessionRemoved) {
              tsfnSessionRemoved.BlockingCall([removedId](Napi::Env env, Napi::Function jsCallback) {
                jsCallback.Call({Napi::String::New(env, wstring_to_string(removedId))});
              });
            }
            
            // Unregister events
            UnregisterSessionEvents(removedId);
          }
        }
      });
    }
    else if (eventName == "playbackstatechanged") {
      // Store thread-safe function
      tsfnPlaybackStateChanged = std::move(tsfn);
      
      // Register for playback state changes on all current sessions
      auto sessions = sessionManager.GetSessions();
      for (auto session : sessions) {
        auto id = session.SourceAppUserModelId();
        std::wstring appId(hstring_to_wstring(id));
        
        // Check if we already have event tokens for this session
        std::lock_guard<std::mutex> lock(sessionsLock);
        if (sessionEventTokens.find(appId) == sessionEventTokens.end()) {
          RegisterSessionEvents(session);
        } else {
          // Register playback info changed event
          auto token = session.PlaybackInfoChanged([this, id](auto&& session, auto&& args) {
            if (tsfnPlaybackStateChanged) {
              tsfnPlaybackStateChanged.BlockingCall([id](Napi::Env env, Napi::Function jsCallback) {
                jsCallback.Call({Napi::String::New(env, hstring_to_string(id))});
              });
            }
          });
          
          sessionEventTokens[appId].push_back(token);
        }
      }
    }
    else if (eventName == "timelinepropertieschanged") {
      // Store thread-safe function
      tsfnTimelinePropertiesChanged = std::move(tsfn);
      
      // Register for timeline properties changes on all current sessions
      auto sessions = sessionManager.GetSessions();
      for (auto session : sessions) {
        auto id = session.SourceAppUserModelId();
        std::wstring appId(hstring_to_wstring(id));
        
        // Check if we already have event tokens for this session
        std::lock_guard<std::mutex> lock(sessionsLock);
        if (sessionEventTokens.find(appId) == sessionEventTokens.end()) {
          RegisterSessionEvents(session);
        } else {
          // Register timeline properties changed event
          auto token = session.TimelinePropertiesChanged([this, id](auto&& session, auto&& args) {
            if (tsfnTimelinePropertiesChanged) {
              tsfnTimelinePropertiesChanged.BlockingCall([id](Napi::Env env, Napi::Function jsCallback) {
                jsCallback.Call({Napi::String::New(env, hstring_to_string(id))});
              });
            }
          });
          
          sessionEventTokens[appId].push_back(token);
        }
      }
    }
    else if (eventName == "mediapropertieschanged") {
      // Store thread-safe function
      tsfnMediaPropertiesChanged = std::move(tsfn);
      
      // Register for media properties changes on all current sessions
      auto sessions = sessionManager.GetSessions();
      for (auto session : sessions) {
        auto id = session.SourceAppUserModelId();
        std::wstring appId(hstring_to_wstring(id));
        
        // Check if we already have event tokens for this session
        std::lock_guard<std::mutex> lock(sessionsLock);
        if (sessionEventTokens.find(appId) == sessionEventTokens.end()) {
          RegisterSessionEvents(session);
        } else {
          // Register media properties changed event
          auto token = session.MediaPropertiesChanged([this, id](auto&& session, auto&& args) {
            if (tsfnMediaPropertiesChanged) {
              tsfnMediaPropertiesChanged.BlockingCall([id](Napi::Env env, Napi::Function jsCallback) {
                jsCallback.Call({Napi::String::New(env, hstring_to_string(id))});
              });
            }
          });
          
          sessionEventTokens[appId].push_back(token);
        }
      }
    }
    else {
      Napi::Error::New(env, "Unknown event: " + eventName).ThrowAsJavaScriptException();
      return env.Undefined();
    }
  }
  catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (const winrt::hresult_error& e) {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
  }
  
  return env.Undefined();
}

Napi::Value SMTCMedia::Off(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected event name").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  
  std::string eventName = info[0].As<Napi::String>().Utf8Value();
  
  try {
    if (eventName == "sessionadded") {
      if (sessionAddedToken.value != 0) {
        sessionManager.SessionsChanged(sessionAddedToken);
        sessionAddedToken = {};
      }
      if (tsfnSessionAdded) tsfnSessionAdded.Release();
    }
    else if (eventName == "sessionremoved") {
      if (sessionRemovedToken.value != 0) {
        sessionManager.SessionsChanged(sessionRemovedToken);
        sessionRemovedToken = {};
      }
      if (tsfnSessionRemoved) tsfnSessionRemoved.Release();
    }
    else if (eventName == "playbackstatechanged") {
      if (tsfnPlaybackStateChanged) tsfnPlaybackStateChanged.Release();
    }
    else if (eventName == "timelinepropertieschanged") {
      if (tsfnTimelinePropertiesChanged) tsfnTimelinePropertiesChanged.Release();
    }
    else if (eventName == "mediapropertieschanged") {
      if (tsfnMediaPropertiesChanged) tsfnMediaPropertiesChanged.Release();
    }
    else {
      Napi::Error::New(env, "Unknown event: " + eventName).ThrowAsJavaScriptException();
    }
  }
  catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (const winrt::hresult_error& e) {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
  }
  
  return env.Undefined();
}

void SMTCMedia::RegisterSessionEvents(GlobalSystemMediaTransportControlsSession const& session) {
  auto id = session.SourceAppUserModelId();
  std::wstring appId = hstring_to_wstring(id);
  std::string appIdStr = hstring_to_string(id); // Convert to std::string for JavaScript
  
  std::lock_guard<std::mutex> lock(sessionsLock);
  
  // Skip if already registered
  if (sessionEventTokens.find(appId) != sessionEventTokens.end()) {
    return;
  }
  
  std::vector<event_token> tokens;
  
  // Register for playback info changed if we have a listener
  if (tsfnPlaybackStateChanged) {
    auto token = session.PlaybackInfoChanged([this, appIdStr](auto&& session, auto&& args) {
      if (tsfnPlaybackStateChanged) {
        tsfnPlaybackStateChanged.BlockingCall([appIdStr](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({Napi::String::New(env, appIdStr)});
        });
      }
    });
    tokens.push_back(token);
  }
  
  // Register for timeline properties changed if we have a listener
  if (tsfnTimelinePropertiesChanged) {
    auto token = session.TimelinePropertiesChanged([this, appIdStr](auto&& session, auto&& args) {
      if (tsfnTimelinePropertiesChanged) {
        tsfnTimelinePropertiesChanged.BlockingCall([appIdStr](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({Napi::String::New(env, appIdStr)});
        });
      }
    });
    tokens.push_back(token);
  }
  
  // Register for media properties changed if we have a listener
  if (tsfnMediaPropertiesChanged) {
    auto token = session.MediaPropertiesChanged([this, appIdStr](auto&& session, auto&& args) {
      if (tsfnMediaPropertiesChanged) {
        tsfnMediaPropertiesChanged.BlockingCall([appIdStr](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({Napi::String::New(env, appIdStr)});
        });
      }
    });
    tokens.push_back(token);
  }
  
  // Store tokens
  sessionEventTokens[appId] = std::move(tokens);
}

void SMTCMedia::UnregisterSessionEvents(std::wstring const& sourceAppUserModelId) {
  std::lock_guard<std::mutex> lock(sessionsLock);
  
  // Remove from the map
  sessionEventTokens.erase(sourceAppUserModelId);
}

Napi::Value SMTCMedia::GetSessions(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try {
    auto sessions = sessionManager.GetSessions();
    Napi::Array result = Napi::Array::New(env, sessions.Size());

    uint32_t i = 0;
    for (auto session : sessions) {
      result.Set(i++, CreateSessionObject(env, session));
    }

    return result;
  }
  catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
  catch (const winrt::hresult_error& e) {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Value SMTCMedia::GetCurrentSession(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try {
    auto currentSession = sessionManager.GetCurrentSession();
    if (currentSession) {
      return CreateSessionObject(env, currentSession);
    }
    return env.Null();
  }
  catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
  catch (const winrt::hresult_error& e) {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Value SMTCMedia::GetSessionInfo(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  try {
    if (info.Length() < 1 || !info[0].IsString()) {
      Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
      return env.Null();
    }

    std::string sessionId = info[0].As<Napi::String>().Utf8Value();
    auto sessions = sessionManager.GetSessions();

    for (auto session : sessions) {
      if (hstring_to_string(session.SourceAppUserModelId()) == sessionId) {
        return CreateSessionObject(env, session);
      }
    }

    return env.Null();
  }
  catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
  catch (const winrt::hresult_error& e) {
    Napi::Error::New(env, winrt::to_string(e.message())).ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Object SMTCMedia::CreateSessionObject(Napi::Env env, GlobalSystemMediaTransportControlsSession const& session) {
  Napi::Object obj = Napi::Object::New(env);
  
  try {
    // Basic session info
    obj.Set("sourceAppUserModelId", Napi::String::New(env, hstring_to_string(session.SourceAppUserModelId())));
    
    // Media properties
    auto mediaPropertiesTask = session.TryGetMediaPropertiesAsync();
    auto mediaProperties = mediaPropertiesTask.get();
    if (mediaProperties) {
      obj.Set("mediaProperties", CreateMediaPropertiesObject(env, mediaProperties));
    }
    
    // Timeline properties
    auto timelineProperties = session.GetTimelineProperties();
    obj.Set("timelineProperties", CreateTimelinePropertiesObject(env, timelineProperties));
    
    // Playback info
    auto playbackInfo = session.GetPlaybackInfo();
    obj.Set("playbackInfo", CreatePlaybackInfoObject(env, playbackInfo));
  }
  catch (const std::exception& e) {
    // Just log the error but continue
    obj.Set("error", Napi::String::New(env, e.what()));
  }
  catch (const winrt::hresult_error& e) {
    // Just log the error but continue
    obj.Set("error", Napi::String::New(env, winrt::to_string(e.message())));
  }
  
  return obj;
}

Napi::Object SMTCMedia::CreateMediaPropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionMediaProperties const& properties) {
  Napi::Object obj = Napi::Object::New(env);
  
  obj.Set("title", Napi::String::New(env, hstring_to_string(properties.Title())));
  obj.Set("artist", Napi::String::New(env, hstring_to_string(properties.Artist())));
  obj.Set("albumTitle", Napi::String::New(env, hstring_to_string(properties.AlbumTitle())));
  obj.Set("albumArtist", Napi::String::New(env, hstring_to_string(properties.AlbumArtist())));
  obj.Set("trackNumber", Napi::Number::New(env, properties.TrackNumber()));
  
  // Handle genres collection safely
  auto genres = properties.Genres();
  if (genres.Size() > 0) {
    obj.Set("genres", Napi::String::New(env, hstring_to_string(genres.GetAt(0))));
  } else {
    obj.Set("genres", Napi::String::New(env, ""));
  }
  
  // Handle playback type safely
  auto playbackType = properties.PlaybackType();
  if (playbackType) {
    obj.Set("playbackType", Napi::Number::New(env, static_cast<int>(playbackType.Value())));
  } else {
    obj.Set("playbackType", Napi::Number::New(env, -1));
  }
  
  return obj;
}

Napi::Object SMTCMedia::CreateTimelinePropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionTimelineProperties const& properties) {
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

Napi::Object SMTCMedia::CreatePlaybackInfoObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionPlaybackInfo const& playbackInfo) {
  Napi::Object obj = Napi::Object::New(env);
  
  obj.Set("playbackStatus", Napi::Number::New(env, static_cast<int>(playbackInfo.PlaybackStatus())));
  
  // Handle playback type safely
  auto playbackType = playbackInfo.PlaybackType();
  if (playbackType) {
    obj.Set("playbackType", Napi::Number::New(env, static_cast<int>(playbackType.Value())));
  } else {
    obj.Set("playbackType", Napi::Number::New(env, -1));
  }
  
  // Handle shuffle active safely
  auto isShuffleActive = playbackInfo.IsShuffleActive();
  if (isShuffleActive) {
    obj.Set("isShuffleActive", Napi::Boolean::New(env, isShuffleActive.Value()));
  } else {
    obj.Set("isShuffleActive", Napi::Boolean::New(env, false));
  }
  
  // Handle auto repeat mode safely
  auto autoRepeatMode = playbackInfo.AutoRepeatMode();
  if (autoRepeatMode) {
    obj.Set("autoRepeatMode", Napi::Number::New(env, static_cast<int>(autoRepeatMode.Value())));
  } else {
    obj.Set("autoRepeatMode", Napi::Number::New(env, -1));
  }
  
  // Handle controls safely
  auto controls = playbackInfo.Controls();
  int controlsValue = 0;
  if (controls.IsPlayEnabled()) controlsValue |= 1;
  if (controls.IsPauseEnabled()) controlsValue |= 2;
  if (controls.IsStopEnabled()) controlsValue |= 4;
  if (controls.IsNextEnabled()) controlsValue |= 8;
  if (controls.IsPreviousEnabled()) controlsValue |= 16;
  
  obj.Set("controls", Napi::Number::New(env, controlsValue));
  
  return obj;
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return SMTCMedia::Init(env, exports);
}

NODE_API_MODULE(smtc, InitAll)