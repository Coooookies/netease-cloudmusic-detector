import pkg from "../addon/smtc/index.js"
const { SMTCMonitor } = pkg

const smtcMonitor = new SMTCMonitor()

smtcMonitor.onSessionAdded((error, session) => {
  console.log("Session added:", session)
  try {
    smtcMonitor.getSessionById(session)
  } catch (error) {
    console.log(error)
  }
})

smtcMonitor.onSessionRemoved((error, session) => {
  console.log("Session removed:", session)
  try {
    smtcMonitor.getSessionById(session)
  } catch (error) {
    console.log(error)
  }
})

smtcMonitor.onMediaPropertiesChanged((error, session) => {
  console.log("Media properties changed:", session)
  try {
    smtcMonitor.getSessionById(session)
  } catch (error) {
    console.log(error)
  }
})

smtcMonitor.onPlaybackInfoChanged((error, session) => {
  console.log("Playback info changed:", session)
  try {
    smtcMonitor.getSessionById(session)
  } catch (error) {
    console.log(error)
  }
})

smtcMonitor.onTimelinePropertiesChanged((error, session) => {
  console.log("Timeline properties changed:", session)
  try {
    smtcMonitor.getSessionById(session)
  } catch (error) {
    console.log(error)
  }
})

smtcMonitor.initialize()
console.log(smtcMonitor.getCurrentSession(), smtcMonitor.getAllSessions())
