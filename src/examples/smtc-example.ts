// Example of using the SMTC (System Media Transport Controls) addon
// This file demonstrates how to use the native addon to query media playback information

import { createRequire } from "node:module"

// Import the native addon

const require = createRequire(import.meta.url)
const smtcAddon = require("../../build/Release/smtc.node")

// Define TypeScript interfaces for the returned objects
interface MediaProperties {
  title: string
  artist: string
  albumTitle: string
  albumArtist: string
  trackNumber: number
  genres: string
  playbackType: number
}

interface TimelineProperties {
  startTimeInSeconds: number
  endTimeInSeconds: number
  positionInSeconds: number
  minSeekTimeInSeconds: number
  maxSeekTimeInSeconds: number
}

interface PlaybackInfo {
  playbackStatus: number
  playbackType: number
  isShuffleActive: boolean
  autoRepeatMode: number
  controls: number
}

interface MediaSession {
  sourceAppUserModelId: string
  mediaProperties?: MediaProperties
  timelineProperties: TimelineProperties
  playbackInfo: PlaybackInfo
  error?: string
}

// Enum for playback status
enum PlaybackStatus {
  Closed = 0,
  Opened = 1,
  Changing = 2,
  Stopped = 3,
  Playing = 4,
  Paused = 5,
}

// Event names
enum SMTCEvent {
  SessionAdded = "sessionadded",
  SessionRemoved = "sessionremoved",
  PlaybackStateChanged = "playbackstatechanged",
  TimelinePropertiesChanged = "timelinepropertieschanged",
  MediaPropertiesChanged = "mediapropertieschanged",
}

// Create an instance of the SMTCMedia class
const smtc = new smtcAddon.SMTCMedia()

// Function to get all media sessions
function getAllSessions(): MediaSession[] {
  try {
    return smtc.getSessions()
  } catch (error) {
    console.error("Error getting sessions:", error)
    return []
  }
}

// Function to get the current media session
function getCurrentSession(): MediaSession | null {
  try {
    return smtc.getCurrentSession()
  } catch (error) {
    console.error("Error getting current session:", error)
    return null
  }
}

// Function to get a specific session by app ID
function getSessionByAppId(appId: string): MediaSession | null {
  try {
    return smtc.getSessionInfo(appId)
  } catch (error) {
    console.error(`Error getting session for app ${appId}:`, error)
    return null
  }
}

// Function to format the session information for display
function formatSessionInfo(session: MediaSession): string {
  if (!session) return "No session information available"

  let result = `App: ${session.sourceAppUserModelId}\n`

  if (session.mediaProperties) {
    const mp = session.mediaProperties
    result += `Title: ${mp.title}\n`
    result += `Artist: ${mp.artist}\n`
    result += `Album: ${mp.albumTitle}\n`
    result += `Album Artist: ${mp.albumArtist}\n`
    result += `Track Number: ${mp.trackNumber}\n`
    result += `Genres: ${mp.genres}\n`
  } else {
    result += "No media properties available\n"
  }

  const tp = session.timelineProperties
  result += `Duration: ${
    Math.round((tp.endTimeInSeconds - tp.startTimeInSeconds) * 100) / 100
  }s\n`
  result += `Position: ${Math.round(tp.positionInSeconds * 100) / 100}s\n`

  const pi = session.playbackInfo
  result += `Playback Status: ${PlaybackStatus[pi.playbackStatus]}\n`
  result += `Shuffle: ${pi.isShuffleActive ? "On" : "Off"}\n`

  if (session.error) {
    result += `Error: ${session.error}\n`
  }

  return result
}

// Function to register event listeners
function setupEventListeners() {
  try {
    // Listen for new sessions
    smtc.on(SMTCEvent.SessionAdded, (appId: string) => {
      try {
        console.log(`\n[EVENT] New session added: ${appId}`)
        const session = getSessionByAppId(appId)
        console.log(session)
        if (session) {
          console.log(formatSessionInfo(session))
        }
      } catch (error) {
        console.error("Error in session added handler:", error)
      }
    })

    // Listen for session removals
    smtc.on(SMTCEvent.SessionRemoved, (appId: string) => {
      try {
        console.log(`\n[EVENT] Session removed: ${appId}`)
      } catch (error) {
        console.error("Error in session removed handler:", error)
      }
    })

    // Listen for playback state changes
    smtc.on(SMTCEvent.PlaybackStateChanged, (appId: string) => {
      try {
        console.log(`\n[EVENT] Playback state changed for: ${appId}`)
        const session = getSessionByAppId(appId)
        if (session) {
          const status = PlaybackStatus[session.playbackInfo.playbackStatus]
          console.log(`New playback status: ${status}`)
        }
      } catch (error) {
        console.error("Error in playback state changed handler:", error)
      }
    })

    // Listen for timeline updates
    smtc.on(SMTCEvent.TimelinePropertiesChanged, (appId: string) => {
      try {
        console.log(`\n[EVENT] Timeline properties changed for: ${appId}`)
        const session = getSessionByAppId(appId)
        if (session) {
          const tp = session.timelineProperties
          console.log(
            `Position: ${Math.round(tp.positionInSeconds * 100) / 100}s / ${
              Math.round((tp.endTimeInSeconds - tp.startTimeInSeconds) * 100) /
              100
            }s`
          )
        }
      } catch (error) {
        console.error("Error in timeline properties changed handler:", error)
      }
    })

    // Listen for media properties changes
    smtc.on(SMTCEvent.MediaPropertiesChanged, (appId: string) => {
      try {
        console.log(`\n[EVENT] Media properties changed for: ${appId}`)
        const session = getSessionByAppId(appId)
        if (session && session.mediaProperties) {
          const mp = session.mediaProperties
          console.log(`Now playing: ${mp.title} - ${mp.artist}`)
        }
      } catch (error) {
        console.error("Error in media properties changed handler:", error)
      }
    })
  } catch (error) {
    console.error("Error setting up event listeners:", error)
  }
}

// Function to remove event listeners
function removeEventListeners() {
  try {
    // Remove all event listeners
    smtc.off(SMTCEvent.SessionAdded)
    smtc.off(SMTCEvent.SessionRemoved)
    smtc.off(SMTCEvent.PlaybackStateChanged)
    smtc.off(SMTCEvent.TimelinePropertiesChanged)
    smtc.off(SMTCEvent.MediaPropertiesChanged)

    console.log("Event listeners removed")
  } catch (error) {
    console.error("Error removing event listeners:", error)
  }
}

function main() {
  console.log("=== Current Media Session ===")
  const currentSession = getCurrentSession()
  if (currentSession) {
    console.log(formatSessionInfo(currentSession))
  } else {
    console.log("No active media session found")
  }

  console.log("\n=== All Media Sessions ===")
  const allSessions = getAllSessions()
  if (allSessions.length > 0) {
    allSessions.forEach((session, index) => {
      console.log(`\n--- Session ${index + 1} ---`)
      console.log(formatSessionInfo(session))
    })
  } else {
    console.log("No media sessions found")
  }

  setupEventListeners()

  process.on("SIGINT", () => {
    console.log("\nRemoving event listeners and exiting...")
    removeEventListeners()
    process.exit(0)
  })
}

// Example of monitoring media sessions for 30 seconds
function runTimedExample() {
  try {
    console.log("Starting 30-second monitoring example...")

    // Set up event listeners
    setupEventListeners()

    // Show initial state
    const allSessions = getAllSessions()
    if (allSessions.length > 0) {
      console.log("\nCurrent media sessions:")
      allSessions.forEach((session, index) => {
        console.log(`\n--- Session ${index + 1} ---`)
        console.log(formatSessionInfo(session))
      })
    } else {
      console.log("No media sessions currently active.")
    }

    console.log("\nMonitoring for 30 seconds...")

    // Clean up after 30 seconds
    setTimeout(() => {
      removeEventListeners()
      console.log("\nMonitoring complete.")
      process.exit(0)
    }, 30000)
  } catch (error) {
    console.error("Error in timed example:", error)
  }
}

// Run the main function to show current state
// and set up continuous monitoring
main()

// Alternatively, use the timed example:
// runTimedExample()
