import pkg from "../addon/smtc/index.js"
const { SmtcMonitor } = pkg

const smtcMonitor = new SmtcMonitor()
smtcMonitor.initialize()

smtcMonitor.onSessionAdded((session) => {
  console.log("Session added:", session)
})

smtcMonitor.onSessionRemoved((session) => {
  console.log("Session removed:", session)
})

smtcMonitor.onMediaPropertiesChanged((session) => {
  console.log("Media properties changed:", session)
})

smtcMonitor.onPlaybackInfoChanged((session) => {
  console.log("Playback info changed:", session)
})

smtcMonitor.onTimelinePropertiesChanged((session) => {
  console.log("Timeline properties changed:", session)
})

console.log(smtcMonitor.getCurrentSession(), smtcMonitor.getAllSessions())
