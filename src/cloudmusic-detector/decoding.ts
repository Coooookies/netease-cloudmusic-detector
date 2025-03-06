export function decode(dataArray: Uint8Array): string {
  const bytesArr = Array.from(dataArray)
  const decodedBytes: number[] = bytesArr.map((byte) => {
    const hexDigit = (Math.floor(byte / 16) ^ ((byte % 16) + 8)) % 16
    return (
      hexDigit * 16 + Math.floor(byte / 64) * 4 + (~Math.floor(byte / 16) & 3)
    )
  })

  let decodedBuffer = new Uint8Array(decodedBytes)

  while (decodedBuffer.length > 0) {
    try {
      return new TextDecoder("utf-8").decode(decodedBuffer)
    } catch (error) {
      if (error instanceof TypeError) {
        decodedBuffer = decodedBuffer.slice(1)
      } else {
        break
      }
    }
  }

  return ""
}
