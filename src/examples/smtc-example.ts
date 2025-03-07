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
  result += `Duration: ${Math.round(
    tp.endTimeInSeconds - tp.startTimeInSeconds
  )}s\n`
  result += `Position: ${Math.round(tp.positionInSeconds)}s\n`

  const pi = session.playbackInfo
  result += `Playback Status: ${PlaybackStatus[pi.playbackStatus]}\n`
  result += `Shuffle: ${pi.isShuffleActive ? "On" : "Off"}\n`

  if (session.error) {
    result += `Error: ${session.error}\n`
  }

  return result
}

// Main function to demonstrate the functionality
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
}

// Run the main function
main()
