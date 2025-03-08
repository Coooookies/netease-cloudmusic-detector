import pkg from "../addon/smtc/index.js"
const { SMTCMonitor } = pkg

const smtcMonitor = new SMTCMonitor()

smtcMonitor.onSessionAdded((error, session) => {
  console.log("Session added:", session.sourceAppId)
})

smtcMonitor.onSessionRemoved((error, sessionId) => {
  console.log("Session removed:", sessionId)
})

smtcMonitor.onMediaPropertiesChanged((error, session) => {
  console.log("Media properties changed:", session.sourceAppId, session.title)
})

smtcMonitor.onPlaybackInfoChanged((error, session) => {
  console.log(
    "Playback info changed:",
    session.sourceAppId,
    session.playbackStatus
  )
})

smtcMonitor.onTimelinePropertiesChanged((error, session) => {
  console.log(
    "Timeline properties changed:",
    session.sourceAppId,
    session.duration,
    session.position
  )
})

smtcMonitor.initialize()
console.log(smtcMonitor.getCurrentSession(), smtcMonitor.getAllSessions())
