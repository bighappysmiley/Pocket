/** Format helpers and small pure utilities. */

export function relativeTime(iso?: string | null): string {
  if (!iso) return ''
  const then = new Date(iso).getTime()
  if (Number.isNaN(then)) return ''
  const diff = Date.now() - then
  const sec = Math.round(diff / 1000)
  if (sec < 60) return 'just now'
  const min = Math.round(sec / 60)
  if (min < 60) return `${min}m ago`
  const hr = Math.round(min / 60)
  if (hr < 24) return `${hr}h ago`
  const day = Math.round(hr / 24)
  if (day < 14) return `${day}d ago`
  return new Date(iso).toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: 'numeric' })
}

export function formatDate(iso?: string | null): string {
  if (!iso) return ''
  return new Date(iso).toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: 'numeric' })
}

export function daysLeft(iso?: string | null): number | null {
  if (!iso) return null
  const end = new Date(iso).getTime()
  if (Number.isNaN(end)) return null
  return Math.max(0, Math.ceil((end - Date.now()) / (1000 * 60 * 60 * 24)))
}

const NAME_RE = /^[\p{L}\p{N} \-']+$/u

export function validateDeviceName(raw: string): string | null {
  const name = raw.trim()
  if (!name) return 'Enter a name'
  if (name.length > 20) return 'Name must be 20 characters or fewer'
  if (!NAME_RE.test(name)) return 'Use letters, numbers, spaces, hyphen, or apostrophe'
  return null
}

export function sanitizeDeviceName(raw: string): string {
  return raw.slice(0, 20)
}

const LOCK_MESSAGE_MAX = 40

export function sanitizeLockMessage(raw: string): string {
  return raw.slice(0, LOCK_MESSAGE_MAX)
}

const A2HS_KEY = 'pocket.a2hs.dismissed_at'
const A2HS_DAYS = 14

export function shouldShowA2HS(): boolean {
  try {
    const raw = localStorage.getItem(A2HS_KEY)
    if (!raw) return true
    const then = Number(raw)
    if (Number.isNaN(then)) return true
    const elapsed = Date.now() - then
    return elapsed > A2HS_DAYS * 24 * 60 * 60 * 1000
  } catch {
    return true
  }
}

export function dismissA2HS(): void {
  try {
    localStorage.setItem(A2HS_KEY, String(Date.now()))
  } catch {
    // ignore
  }
}

export function isStandaloneDisplay(): boolean {
  return (
    window.matchMedia('(display-mode: standalone)').matches ||
    // iOS Safari
    ('standalone' in navigator && Boolean((navigator as Navigator & { standalone?: boolean }).standalone))
  )
}
