#![deny(clippy::all)]

#[macro_use]
extern crate napi_derive;

use napi::{
  bindgen_prelude::*,
  threadsafe_function::{ThreadsafeFunction, ThreadsafeFunctionCallMode},
  JsFunction, Result,
};
use std::{
  collections::HashMap,
  fmt,
  sync::{Arc, Mutex},
  time::{SystemTime, UNIX_EPOCH},
};
use windows::{
  core,
  Foundation::{TimeSpan, TypedEventHandler},
  Media::{
    Control::{
      GlobalSystemMediaTransportControlsSession, GlobalSystemMediaTransportControlsSessionManager,
      GlobalSystemMediaTransportControlsSessionPlaybackStatus,
    },
    MediaPlaybackType,
  },
  Storage::Streams::{Buffer as WinBuffer, DataReader, InputStreamOptions},
};

// 辅助函数用于错误转换，替代 From trait 实现
fn win_to_napi_err<T>(result: core::Result<T>) -> Result<T> {
  result.map_err(|e| Error::new(Status::GenericFailure, e.to_string()))
}

// 移除 Debug 派生特性，手动实现
#[napi(object)]
#[derive(Clone)]
pub struct MediaInfo {
  pub source_app_id: String,
  pub title: String,
  pub artist: String,
  pub album_title: String,
  pub album_artist: String,
  pub genres: Vec<String>,
  pub album_track_count: u32,
  pub track_number: u32,
  #[napi(ts_type = "Buffer | undefined")]
  pub thumbnail: Option<napi::bindgen_prelude::Buffer>,
  pub playback_status: u8,
  pub playback_type: u8,
  pub position: f64,
  pub duration: f64,
  pub last_updated_time: f64,
}

// 手动实现 Debug trait，忽略 thumbnail 字段
impl fmt::Debug for MediaInfo {
  fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    f.debug_struct("MediaInfo")
      .field("source_app_id", &self.source_app_id)
      .field("title", &self.title)
      .field("artist", &self.artist)
      .field("album_title", &self.album_title)
      .field("album_artist", &self.album_artist)
      .field("genres", &self.genres)
      .field("album_track_count", &self.album_track_count)
      .field("track_number", &self.track_number)
      .field("thumbnail", &"[Buffer]") // 忽略实际内容，只显示占位符
      .field("playback_status", &self.playback_status)
      .field("playback_type", &self.playback_type)
      .field("position", &self.position)
      .field("duration", &self.duration)
      .field("last_updated_time", &self.last_updated_time)
      .finish()
  }
}

#[allow(dead_code)]
struct InnerSession {
  session: GlobalSystemMediaTransportControlsSession,
  callbacks: Vec<ThreadsafeFunction<String>>,
}

struct SessionManager {
  sessions: HashMap<String, InnerSession>,
  // 修改回调函数类型为 MediaInfo
  session_added_callbacks: Vec<ThreadsafeFunction<MediaInfo>>,
  session_removed_callbacks: Vec<ThreadsafeFunction<String>>,
  media_props_callbacks: Vec<ThreadsafeFunction<MediaInfo>>,
  playback_info_callbacks: Vec<ThreadsafeFunction<MediaInfo>>,
  timeline_props_callbacks: Vec<ThreadsafeFunction<MediaInfo>>,
}

impl SessionManager {
  fn new() -> Self {
    Self {
      sessions: HashMap::new(),
      session_added_callbacks: Vec::new(),
      session_removed_callbacks: Vec::new(),
      media_props_callbacks: Vec::new(),
      playback_info_callbacks: Vec::new(),
      timeline_props_callbacks: Vec::new(),
    }
  }
}

#[napi(js_name = "SMTCMonitor")]
pub struct SMTCMonitor {
  manager: Arc<Mutex<SessionManager>>,
  smtc_manager: Option<GlobalSystemMediaTransportControlsSessionManager>,
}

// 辅助函数用于 TimeSpan 转换到秒数
fn timespan_to_seconds(ts: TimeSpan) -> f64 {
  // TimeSpan 以 100 纳秒为单位
  ts.Duration as f64 / 10_000_000.0
}

// 修正 Buffer 的使用
fn buffer_to_napi_buffer(win_buffer: &WinBuffer) -> Result<Option<Buffer>> {
  let length = win_to_napi_err(win_buffer.Length())?;
  if length > 0 {
    // 转换为 Vec<u8>
    let mut bytes = vec![0u8; length as usize];
    let data_reader = win_to_napi_err(DataReader::FromBuffer(win_buffer))?;
    win_to_napi_err(data_reader.ReadBytes(&mut bytes))?;

    // 将 Vec<u8> 转换为 napi Buffer
    Ok(Some(bytes.into()))
  } else {
    Ok(None)
  }
}

// 初始化函数，不需要调用 windows::core::init()
#[napi]
impl SMTCMonitor {
  #[napi(constructor)]
  pub fn new() -> Self {
    Self {
      manager: Arc::new(Mutex::new(SessionManager::new())),
      smtc_manager: None,
    }
  }

  #[napi]
  pub fn initialize(&mut self) -> Result<()> {
    let manager = self.get_manager()?;
    let manager_clone = manager.clone();
    let inner_manager = self.manager.clone();

    // 初始化时主动扫描并注册现有会话，解决第三个问题
    self.scan_existing_sessions()?;

    // 监听会话变更事件，在回调内避免使用 NAPI 错误处理
    let _token = win_to_napi_err(
      manager.SessionsChanged(&TypedEventHandler::new(move |_, _| {
        let manager = manager_clone.clone();
        if let Ok(sessions) = manager.GetSessions() {
          let mut inner = inner_manager.lock().unwrap();

          // 检测新加入的会话
          let mut current_ids = Vec::new();

          // 使用 windows 标准错误处理，不用 NAPI 的错误处理
          if let Ok(size) = sessions.Size() {
            for i in 0..size {
              if let Ok(session) = sessions.GetAt(i) {
                if let Ok(id) = session.SourceAppUserModelId() {
                  let id = id.to_string();
                  current_ids.push(id.clone());

                  // 如果是新会话，添加到管理器并触发回调
                  if !inner.sessions.contains_key(&id) {
                    // 使用辅助方法注册会话
                    Self::register_session(
                      &mut inner, 
                      id.clone(), 
                      session
                    );
                  }
                }
              }
            }

            // 检测已移除的会话
            let mut removed_ids = Vec::new();
            for id in inner.sessions.keys() {
              if !current_ids.contains(id) {
                removed_ids.push(id.clone());
              }
            }

            for id in removed_ids {
              inner.sessions.remove(&id);
              // 通知会话已移除
              for callback in &inner.session_removed_callbacks {
                callback.call(Ok(id.clone()), ThreadsafeFunctionCallMode::Blocking);
              }
            }
          }
        }
        Ok(())
      })),
    )?;

    Ok(())
  }

  // 修改回调参数类型
  #[napi(ts_args_type = "callback: (error:unknown, media: MediaInfo) => void")]
  pub fn on_session_added(&mut self, callback: JsFunction) -> Result<()> {
    let tsfn: ThreadsafeFunction<MediaInfo> =
      callback.create_threadsafe_function(0, |ctx| Ok(vec![ctx.value]))?;
    let mut inner = self.manager.lock().unwrap();
    inner.session_added_callbacks.push(tsfn);
    Ok(())
  }

  #[napi(ts_args_type = "callback: (error:unknown, sourceAppId: MediaInfo) => void")]
  pub fn on_session_removed(&mut self, callback: JsFunction) -> Result<()> {
    // 会话移除事件仍保留原样，只传递 ID
    let tsfn: ThreadsafeFunction<String> =
      callback.create_threadsafe_function(0, |ctx| Ok(vec![ctx.value]))?;
    let mut inner = self.manager.lock().unwrap();
    inner.session_removed_callbacks.push(tsfn);
    Ok(())
  }

  #[napi(ts_args_type = "callback: (error:unknown, media: MediaInfo) => void")]
  pub fn on_media_properties_changed(&mut self, callback: JsFunction) -> Result<()> {
    let tsfn: ThreadsafeFunction<MediaInfo> =
      callback.create_threadsafe_function(0, |ctx| Ok(vec![ctx.value]))?;
    let mut inner = self.manager.lock().unwrap();
    inner.media_props_callbacks.push(tsfn);
    Ok(())
  }

  #[napi(ts_args_type = "callback: (error:unknown, media: MediaInfo) => void")]
  pub fn on_playback_info_changed(&mut self, callback: JsFunction) -> Result<()> {
    let tsfn: ThreadsafeFunction<MediaInfo> =
      callback.create_threadsafe_function(0, |ctx| Ok(vec![ctx.value]))?;
    let mut inner = self.manager.lock().unwrap();
    inner.playback_info_callbacks.push(tsfn);
    Ok(())
  }

  #[napi(ts_args_type = "callback: (error:unknown, media: MediaInfo) => void")]
  pub fn on_timeline_properties_changed(&mut self, callback: JsFunction) -> Result<()> {
    let tsfn: ThreadsafeFunction<MediaInfo> =
      callback.create_threadsafe_function(0, |ctx| Ok(vec![ctx.value]))?;
    let mut inner = self.manager.lock().unwrap();
    inner.timeline_props_callbacks.push(tsfn);
    Ok(())
  }

  // 修改异步方法为同步方法，避免 Send trait 问题
  #[napi]
  pub fn get_current_session(&self) -> Result<Option<MediaInfo>> {
    let manager = self.get_manager()?;
    if let Ok(session) = manager.GetCurrentSession() {
      return self.session_to_media_info_sync(&session);
    }
    Ok(None)
  }

  #[napi]
  pub fn get_all_sessions(&self) -> Result<Vec<MediaInfo>> {
    let manager = self.get_manager()?;

    // 使用同步方式处理，避免 Send 问题
    let mut result = Vec::new();
    if let Ok(sessions) = manager.GetSessions() {
      if let Ok(size) = sessions.Size() {
        for i in 0..size {
          if let Ok(session) = sessions.GetAt(i) {
            if let Ok(Some(info)) = self.session_to_media_info_sync(&session) {
              result.push(info);
            }
          }
        }
      }
    }

    Ok(result)
  }

  #[napi]
  pub fn get_session_by_id(&self, source_app_id: String) -> Result<Option<MediaInfo>> {
    let manager = self.get_manager()?;

    if let Ok(sessions) = manager.GetSessions() {
      if let Ok(size) = sessions.Size() {
        for i in 0..size {
          if let Ok(session) = sessions.GetAt(i) {
            if let Ok(id) = session.SourceAppUserModelId() {
              if id.to_string() == source_app_id {
                return self.session_to_media_info_sync(&session);
              }
            }
          }
        }
      }
    }

    Ok(None)
  }

  fn get_manager(&self) -> Result<GlobalSystemMediaTransportControlsSessionManager> {
    if let Some(manager) = &self.smtc_manager {
      return Ok(manager.clone());
    }

    match GlobalSystemMediaTransportControlsSessionManager::RequestAsync() {
      Ok(operation) => match operation.get() {
        Ok(manager) => Ok(manager),
        Err(e) => Err(Error::new(Status::GenericFailure, e.to_string())),
      },
      Err(e) => Err(Error::new(Status::GenericFailure, e.to_string())),
    }
  }

  // 修改原有方法，使用新的公共方法
  fn session_to_media_info_sync(
    &self,
    session: &GlobalSystemMediaTransportControlsSession,
  ) -> Result<Option<MediaInfo>> {
    // 使用公共方法获取 MediaInfo
    Self::get_media_info_for_session(session)
  }

  // 添加扫描现有会话的辅助方法
  fn scan_existing_sessions(&mut self) -> Result<()> {
    let manager = self.get_manager()?;
    if let Ok(sessions) = manager.GetSessions() {
      let mut inner = self.manager.lock().unwrap();

      if let Ok(size) = sessions.Size() {
        for i in 0..size {
          if let Ok(session) = sessions.GetAt(i) {
            if let Ok(id) = session.SourceAppUserModelId() {
              let id = id.to_string();

              // 如果是新会话，添加到管理器并设置监听
              if !inner.sessions.contains_key(&id) {
                // 使用辅助方法注册会话
                Self::register_session(
                  &mut inner,
                  id.clone(),
                  session
                );
              }
            }
          }
        }
      }
    }

    Ok(())
  }

  // 添加新的辅助方法处理会话注册和监听
  // 修改 register_session 方法，现在获取 MediaInfo 对象并传递给回调
  fn register_session(
    inner: &mut SessionManager,
    id: String,
    session: GlobalSystemMediaTransportControlsSession
  ) {
    // 为每个回调单独克隆会话对象和回调列表
    let media_session_clone = session.clone();
    let media_props_callbacks = inner.media_props_callbacks.clone();
    
    // 为 PlaybackInfoChanged 创建独立的会话克隆
    let playback_session_clone = session.clone();
    let playback_info_callbacks = inner.playback_info_callbacks.clone();
    
    // 为 TimelinePropertiesChanged 创建独立的会话克隆
    let timeline_session_clone = session.clone();
    let timeline_props_callbacks = inner.timeline_props_callbacks.clone();
    
    // 媒体属性改变事件
    let _media_token =
      session.MediaPropertiesChanged(&TypedEventHandler::new(move |_, _| {
        // 尝试获取最新的 MediaInfo
        if let Ok(Some(media_info)) = Self::get_media_info_for_session(&media_session_clone) {
          for callback in &media_props_callbacks {
            callback.call(Ok(media_info.clone()), ThreadsafeFunctionCallMode::Blocking);
          }
        }
        Ok(())
      }));

    // 播放信息改变事件
    let _playback_token =
      session.PlaybackInfoChanged(&TypedEventHandler::new(move |_, _| {
        // 尝试获取最新的 MediaInfo
        if let Ok(Some(media_info)) = Self::get_media_info_for_session(&playback_session_clone) {
          for callback in &playback_info_callbacks {
            callback.call(Ok(media_info.clone()), ThreadsafeFunctionCallMode::Blocking);
          }
        }
        Ok(())
      }));

    // 时间线改变事件
    let _timeline_token =
      session.TimelinePropertiesChanged(&TypedEventHandler::new(move |_, _| {
        // 尝试获取最新的 MediaInfo
        if let Ok(Some(media_info)) = Self::get_media_info_for_session(&timeline_session_clone) {
          for callback in &timeline_props_callbacks {
            callback.call(Ok(media_info.clone()), ThreadsafeFunctionCallMode::Blocking);
          }
        }
        Ok(())
      }));

    // 将会话保存到内部状态
    inner.sessions.insert(
      id.clone(),
      InnerSession {
        session: session.clone(),
        callbacks: Vec::new(),
      },
    );

    // 通知会话已添加，并传递完整的 MediaInfo
    // 尝试获取 MediaInfo
    if let Ok(Some(media_info)) = Self::get_media_info_for_session(&session) {
      for callback in &inner.session_added_callbacks {
        callback.call(Ok(media_info.clone()), ThreadsafeFunctionCallMode::Blocking);
      }
    }
  }

  // 添加用于回调的公共方法，获取会话的 MediaInfo
  fn get_media_info_for_session(
    session: &GlobalSystemMediaTransportControlsSession
  ) -> Result<Option<MediaInfo>> {
    if let Ok(media_props) = session.TryGetMediaPropertiesAsync() {
      let media_props = win_to_napi_err(media_props.get())?;

      let title = win_to_napi_err(media_props.Title())?.to_string();
      let artist = win_to_napi_err(media_props.Artist())?.to_string();
      let album_title = win_to_napi_err(media_props.AlbumTitle())?.to_string();
      let album_artist = win_to_napi_err(media_props.AlbumArtist())?.to_string();

      let mut genres = Vec::new();
      if let Ok(genre_list) = media_props.Genres() {
        for i in 0..win_to_napi_err(genre_list.Size())? {
          if let Ok(genre) = genre_list.GetAt(i) {
            genres.push(genre.to_string());
          }
        }
      }

      let album_track_count = win_to_napi_err(media_props.AlbumTrackCount())?;
      let track_number = win_to_napi_err(media_props.TrackNumber())?;

      // 缩略图读取逻辑
      let thumbnail = if let Ok(thumbnail_ref) = media_props.Thumbnail() {
        if let Ok(stream_op) = thumbnail_ref.OpenReadAsync() {
          if let Ok(stream) = win_to_napi_err(stream_op.get()) {
            // 创建合适的 Buffer 来接收数据
            let buffer = win_to_napi_err(WinBuffer::Create(1024 * 1024))?;

            // 需要传递引用
            if let Ok(read_op) = stream.ReadAsync(
              &buffer,
              win_to_napi_err(buffer.Capacity())?,
              InputStreamOptions::None,
            ) {
              if win_to_napi_err(read_op.get()).is_ok() {
                buffer_to_napi_buffer(&buffer)?
              } else {
                None
              }
            } else {
              None
            }
          } else {
            None
          }
        } else {
          None
        }
      } else {
        None
      };

      // 获取播放信息
      let playback_info = win_to_napi_err(session.GetPlaybackInfo())?;

      let playback_status = match win_to_napi_err(playback_info.PlaybackStatus())? {
        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed => 0,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened => 1,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing => 2,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped => 3,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing => 4,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused => 5,
        _ => 0,
      };

      let playback_type = match playback_info.PlaybackType() {
        Ok(pt_ref) => {
          if let Ok(pt) = pt_ref.Value() {
            match pt {
              MediaPlaybackType::Unknown => 0,
              MediaPlaybackType::Music => 1,
              MediaPlaybackType::Video => 2,
              MediaPlaybackType::Image => 3,
              _ => 0,
            }
          } else {
            0
          }
        }
        Err(_) => 0,
      };

      // 获取时间线信息
      let timeline_props = win_to_napi_err(session.GetTimelineProperties())?;
      // 正确处理 TimeSpan
      let position = timespan_to_seconds(win_to_napi_err(timeline_props.Position())?);
      let duration = timespan_to_seconds(win_to_napi_err(timeline_props.EndTime())?);
      let last_updated_time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as f64;

      let source_app_id = win_to_napi_err(session.SourceAppUserModelId())?.to_string();

      Ok(Some(MediaInfo {
        source_app_id,
        title,
        artist,
        album_title,
        album_artist,
        genres,
        album_track_count: album_track_count.try_into().unwrap_or(0),
        track_number: track_number.try_into().unwrap_or(0),
        thumbnail,
        playback_status,
        playback_type,
        position,
        duration,
        last_updated_time,
      }))
    } else {
      Ok(None)
    }
  }
}
