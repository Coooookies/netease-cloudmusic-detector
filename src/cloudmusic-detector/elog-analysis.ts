import os from "node:os"
import { CLOUDMUSIC_ELOG_MATCHES } from "./constant.js"
import { type PlayingStatusType } from "./types.js"

export function getElogHeader(row: string) {
  const regex =
    /^\[(\d+):(\d+):(\d{4}\/\d{6}:\d+):([A-Z]+):([a-zA-Z0-9._-]+)\((\d+)\)\]\s+\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\]/

  const [origin, pid, tid, timestamp, type, src, lr, datetime] =
    row.match(regex) || []

  if (!origin) {
    return null
  }

  const [_, startupTime] = timestamp.split(":")

  const uptime = os.uptime() * 1000
  const currentTime = +Date.now()
  const bootTime = currentTime - uptime
  const realLogTime = +startupTime + bootTime

  return {
    pid,
    tid,
    timestamp: realLogTime,
    type,
    src,
    lr,
    datetime,
  }
}

export function getElogType(row: string): PlayingStatusType | null {
  for (const [key, { rule }] of Object.entries(CLOUDMUSIC_ELOG_MATCHES)) {
    if (rule(row)) {
      return key as PlayingStatusType
    }
  }

  return null
}
