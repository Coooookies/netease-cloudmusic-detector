import { CloudmusicDetector } from "./cloudmusic-detector/index.js"

const detector = new CloudmusicDetector()
const logger = () => {
  if (!detector.status.playing) {
    return
  }

  console.log(
    `Playing`,
    `${`${Math.floor(detector.status.position / 60)}`.padStart(
      2,
      "0"
    )}:${`${Math.round(detector.status.position % 60)}`.padStart(2, "0")}`
  )
}

detector.start().then(() => {
  console.log(detector.status)
})

detector.on("play", (songId) => {
  console.log("Song Id", songId)
  console.log(detector.status)
})

detector.on("status", (playing) => {
  console.log("Song Playing", playing)
})

detector.on("position", (position) => {
  console.log("Song Position", position)
})

setInterval(logger, 1000)
