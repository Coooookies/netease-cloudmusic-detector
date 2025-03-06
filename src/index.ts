import { CloudmusicDetector } from "./cloudmusic-detector/index.js"

const detector = new CloudmusicDetector()

detector.start()
detector.on("play", (songId) => {
  console.log("Song Id", songId)
})

detector.on("status", (playing) => {
  console.log("Song Playing", playing)
})

detector.on("position", (position) => {
  console.log("Song Position", position)
})

setInterval(() => {
  console.log(
    `Song Id`,
    `${detector.status.id} - ${
      detector.status.playing ? "Playing" : "Paused"
    } | ${`${Math.floor(detector.status.position / 60)}`.padStart(
      2,
      "0"
    )}:${`${Math.round(detector.status.position % 60)}`.padStart(2, "0")}`
  )
}, 1000)
