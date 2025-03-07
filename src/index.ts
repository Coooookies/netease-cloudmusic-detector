import pkg from "../addon/smtc/index.js"
const { SmtcMonitor } = pkg

const smtcMonitor = new SmtcMonitor()
smtcMonitor.initialize()

smtcMonitor.onSessionAdded((error, session) => {
  console.log("Session added:", session)
})

smtcMonitor.onSessionRemoved((error, session) => {
  console.log("Session removed:", session)
})

smtcMonitor.onMediaPropertiesChanged((error, session) => {
  console.log("Media properties changed:", session)
})

smtcMonitor.onPlaybackInfoChanged((error, session) => {
  console.log("Playback info changed:", session)
})

smtcMonitor.onTimelinePropertiesChanged((error, session) => {
  console.log("Timeline properties changed:", session)
  console.log(smtcMonitor.getSessionById(session))
})

console.log(smtcMonitor.getCurrentSession(), smtcMonitor.getAllSessions())
