import { CLOUDMUSIC_ELOG_MATCHES } from "./constant.js"

export type PlayingStatusID = string

export type PlayingStatusArtist = {
  id: PlayingStatusID
  name: string
  alia: string[]
  alias: string[]
  transName: string
}

export type PlayingStatusAlbum = {
  id: PlayingStatusID
  name: string
  description: string
  trackCount: number
  subscribedCount: number
  commentCount: number
  commentThreadId: string
  algorithm: string
  size: number
  albumName: string
  picId: string
  picUrl: string
  cover: string
  alias: string[]
  transNames: string[]
  explicit: boolean
}

export type PlayingStatusFreeTrialPrivilege = {
  resConsumable: boolean
  userConsumable: boolean
  listenType: null
  cannotListenReason: null
  playReason: null
  freeLimitTagType: null
}

export type PlayingStatusPrivilege = {
  id: PlayingStatusID
  fee: number
  payed: number
  maxPlayBr: number
  maxDownBr: number
  commentPriv: number
  cloudSong: number
  toast: boolean
  flag: number
  now: number
  maxSongBr: number
  maxFreeBr: number
  sharePriv: number
  status: number
  subPriv: number
  maxSongLevel: number
  maxDownLevel: number
  maxFreeLevel: number
  maxPlayLevel: number
  freeTrialPrivilege: PlayingStatusFreeTrialPrivilege
}

export type PlayingStatusTrack = {
  id: PlayingStatusID
  commentThreadId: string
  copyrightId: string
  duration: number
  mvid: string
  name: string
  status: number
  fee: number
  songType: number
  noCopyrightRcmd: null
  originCoverType: number
  mark: number
  artists: PlayingStatusArtist[]
  privilege: PlayingStatusPrivilege
  album: PlayingStatusAlbum

  algorithm: string
}

export type PlayingStatusLocalTrack = Record<string, unknown>

export type PlayingStatusSourceData = {
  id: PlayingStatusID
  playCount: number
  name: string
  coverImgUrl: string
}

export type PlayingStatusFromInfo = {
  originalScene: string
  originalResourceType: string
  computeSourceResourceType: string
  sourceData: PlayingStatusSourceData
  trialMode: number
}

// Reference info types
export type PlayingStatusReferInfo = {
  addrefer: string
  multirefers: string[]
}

// Main component types
export type PlayingStatusTrackIn = {
  displayOrder: number
  randomOrder: number
  isPlayedOnce: boolean
  ai: string
  aiRcmd: boolean
  scene: string
  href: string
  text: string
  localTrack: PlayingStatusLocalTrack
  track: PlayingStatusTrack
  resourceType: string
  fromInfo: PlayingStatusFromInfo
  resourceId: PlayingStatusID
  trackId: PlayingStatusID
  referInfo: PlayingStatusReferInfo
}

export type PlayingStatus = {
  trackIn: PlayingStatusTrackIn
  playingState: number
  noAddToHistory: boolean
  flag: number
  fromType: string
  triggerScene: string
}

export type PlayingStatusType = keyof typeof CLOUDMUSIC_ELOG_MATCHES
