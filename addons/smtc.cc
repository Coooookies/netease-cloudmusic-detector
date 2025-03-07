#include <napi.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <string>
#include <vector>
#include <memory>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

class SMTCMedia : public Napi::ObjectWrap<SMTCMedia> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  SMTCMedia(const Napi::CallbackInfo& info);
  ~SMTCMedia();

private:
  static Napi::FunctionReference constructor;
  
  // SMTC session manager
  GlobalSystemMediaTransportControlsSessionManager sessionManager{ nullptr };
  
  // Methods exposed to JavaScript
  Napi::Value GetSessions(const Napi::CallbackInfo& info);
  Napi::Value GetCurrentSession(const Napi::CallbackInfo& info);
  Napi::Value GetSessionInfo(const Napi::CallbackInfo& info);
  
  // Helper methods
  Napi::Object CreateSessionObject(Napi::Env env, GlobalSystemMediaTransportControlsSession const& session);
  Napi::Object CreateMediaPropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionMediaProperties const& properties);
  Napi::Object CreateTimelinePropertiesObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionTimelineProperties const& properties);
  Napi::Object CreatePlaybackInfoObject(Napi::Env env, GlobalSystemMediaTransportControlsSessionPlaybackInfo const& playbackInfo);
};

Napi::FunctionReference SMTCMedia::constructor;

Napi::Object SMTCMedia::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "SMTCMedia", {
    InstanceMethod("getSessions", &SMTCMedia::GetSessions),
    InstanceMethod("getCurrentSession", &SMTCMedia::GetCurrentSession),
    InstanceMethod("getSessionInfo", &SMTCMedia::GetSessionInfo),
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
  // Clean up WinRT
  if (sessionManager != nullptr) {
    sessionManager = nullptr;
  }
  winrt::uninit_apartment();
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
      if (winrt::to_string(session.SourceAppUserModelId()) == sessionId) {
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
    obj.Set("sourceAppUserModelId", Napi::String::New(env, winrt::to_string(session.SourceAppUserModelId())));
    
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
  
  obj.Set("title", Napi::String::New(env, winrt::to_string(properties.Title())));
  obj.Set("artist", Napi::String::New(env, winrt::to_string(properties.Artist())));
  obj.Set("albumTitle", Napi::String::New(env, winrt::to_string(properties.AlbumTitle())));
  obj.Set("albumArtist", Napi::String::New(env, winrt::to_string(properties.AlbumArtist())));
  obj.Set("trackNumber", Napi::Number::New(env, properties.TrackNumber()));
  
  // Handle genres collection safely
  auto genres = properties.Genres();
  if (genres.Size() > 0) {
    obj.Set("genres", Napi::String::New(env, winrt::to_string(genres.GetAt(0))));
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