import { ApiError, type BackupMeta, type Book, type Connector, type ConnectorProvider, type Device, type MeResponse, type MusicTrack, type Note, type PairClaimResult, type PairSession, type PocketList } from './types'

const BUILD_API_BASE = (import.meta.env.VITE_API_BASE as string | undefined)?.replace(/\/$/, '') || ''
const PROD_DEFAULT = 'https://br-super-hill-b40yvyrj-api.compute.c-6.us-east-2.aws.neon.tech'
const STORAGE_KEY = 'pocket_cloud_api_base'
const SESSION_KEY = 'pocket_session_token'

function normalizeOrigin(raw: string): string {
  return raw.trim().replace(/\/$/, '')
}

function readStoredApiBase(): string {
  try {
    const v = localStorage.getItem(STORAGE_KEY)
    return v ? normalizeOrigin(v) : ''
  } catch {
    return ''
  }
}

/** Build-time origin, then optional runtime override from localStorage. */
export function getApiBase(): string {
  const stored = readStoredApiBase()
  if (stored) return stored
  return BUILD_API_BASE || (import.meta.env.PROD ? PROD_DEFAULT : 'http://localhost:8787')
}

/** Persist a Cloud API origin so Pages builds can connect without a rebuild. */
export function setApiBase(origin: string): void {
  const normalized = normalizeOrigin(origin)
  try {
    if (!normalized) localStorage.removeItem(STORAGE_KEY)
    else localStorage.setItem(STORAGE_KEY, normalized)
  } catch {
    // ignore quota / private mode
  }
}

export function clearApiBaseOverride(): void {
  try {
    localStorage.removeItem(STORAGE_KEY)
  } catch {
    // ignore
  }
}

/** Cross-site session token — cookies often fail on mobile Safari (ITP). */
export function getSessionToken(): string {
  try {
    return localStorage.getItem(SESSION_KEY) || ''
  } catch {
    return ''
  }
}

export function setSessionToken(token: string): void {
  try {
    if (!token) localStorage.removeItem(SESSION_KEY)
    else localStorage.setItem(SESSION_KEY, token)
  } catch {
    // ignore
  }
}

export function clearSessionToken(): void {
  setSessionToken('')
}

function isLocalhostOrigin(origin: string): boolean {
  return /^(https?:\/\/)?(localhost|127\.0\.0\.1)(:|\/|$)/i.test(origin)
}

/** True when a usable Cloud API origin is configured (build or runtime). */
export function isApiConfigured(): boolean {
  const base = getApiBase()
  if (!base) return false
  // Build baked empty + no override → still configured in production via PROD_DEFAULT.
  if (!BUILD_API_BASE && !readStoredApiBase() && import.meta.env.PROD) {
    return base === PROD_DEFAULT
  }
  if (import.meta.env.PROD && isLocalhostOrigin(base) && !readStoredApiBase()) return false
  return true
}

type RequestOptions = {
  method?: string
  body?: unknown
  signal?: AbortSignal
}

async function request<T>(path: string, opts: RequestOptions = {}): Promise<T> {
  if (!isApiConfigured() && import.meta.env.PROD) {
    throw new ApiError('Pocket Cloud is not connected yet.', 0, { offline: true, code: 'not_connected' })
  }

  const base = getApiBase()
  const url = `${base}${path.startsWith('/') ? path : `/${path}`}`
  const headers: Record<string, string> = { Accept: 'application/json' }
  if (opts.body !== undefined) headers['Content-Type'] = 'application/json'
  const session = getSessionToken()
  if (session) headers['x-pocket-session'] = session

  let res: Response
  try {
    res = await fetch(url, {
      method: opts.method ?? (opts.body ? 'POST' : 'GET'),
      credentials: 'include',
      headers,
      body: opts.body !== undefined ? JSON.stringify(opts.body) : undefined,
      signal: opts.signal,
    })
  } catch {
    throw new ApiError("You're offline or the server is unreachable.", 0, { offline: true })
  }

  if (res.status === 204) {
    return undefined as T
  }

  const text = await res.text()
  let data: unknown = null
  if (text) {
    try {
      data = JSON.parse(text) as unknown
    } catch {
      data = { message: text }
    }
  }

  if (!res.ok) {
    if (res.status === 401) clearSessionToken()
    const msg =
      (data && typeof data === 'object' && 'message' in data && typeof (data as { message: unknown }).message === 'string'
        ? (data as { message: string }).message
        : null) ||
      (res.status === 401 ? 'Sign in again to continue.' : res.status >= 500 ? 'Something went wrong.' : "You're offline or the server is unreachable.")
    const code =
      data && typeof data === 'object' && 'code' in data && typeof (data as { code: unknown }).code === 'string'
        ? (data as { code: string }).code
        : undefined
    throw new ApiError(msg, res.status, { code })
  }

  return data as T
}

export const api = {
  // Auth
  register(email: string, password: string) {
    return request<{ ok: true; message: string }>('/v1/auth/register', {
      method: 'POST',
      body: { email, password },
    })
  },
  async login(email: string, password: string) {
    const res = await request<{
      ok: true
      session_token?: string
      user: { id: string; email: string; role?: string; is_admin?: boolean }
    }>('/v1/auth/login', {
      method: 'POST',
      body: { email, password },
    })
    if (res.session_token) setSessionToken(res.session_token)
    return res
  },
  async logout() {
    try {
      await request<{ ok: true }>('/v1/auth/logout', { method: 'POST' })
    } finally {
      clearSessionToken()
    }
  },
  me() {
    return request<MeResponse>('/v1/me')
  },
  requestMagicLink(email: string) {
    return request<{ ok: true }>('/v1/auth/magic-link', { method: 'POST', body: { email } })
  },
  async consumeMagicLink(token: string) {
    const res = await request<{ ok: true; session_token?: string }>('/v1/auth/callback', {
      method: 'POST',
      body: { token },
    })
    if (res.session_token) setSessionToken(res.session_token)
    return res
  },

  // Notes
  listNotes() {
    return request<{ notes: Note[] }>('/v1/notes')
  },
  getNote(id: string) {
    return request<Note>(`/v1/notes/${id}`)
  },
  createNote(payload: Partial<Note> & { title: string; body: string; updated_at: string; updated_by_device_id: string }) {
    return request<Note>('/v1/notes', { method: 'POST', body: payload })
  },
  updateNote(id: string, payload: Partial<Note> & { updated_at: string; updated_by_device_id: string }) {
    return request<Note>(`/v1/notes/${id}`, { method: 'PATCH', body: payload })
  },
  deleteNote(id: string, payload: { updated_at: string; updated_by_device_id: string }) {
    return request<void>(`/v1/notes/${id}`, { method: 'DELETE', body: payload })
  },
  createShareLink(noteId: string) {
    return request<{ url: string; expires_at?: string }>(`/v1/notes/${noteId}/share`, { method: 'POST' })
  },
  revokeShareLink(noteId: string) {
    return request<void>(`/v1/notes/${noteId}/share`, { method: 'DELETE' })
  },

  // Lists
  listLists() {
    return request<{ lists: PocketList[] }>('/v1/lists')
  },
  getList(id: string) {
    return request<PocketList>(`/v1/lists/${id}`)
  },
  createList(payload: Partial<PocketList> & { title: string; updated_at: string; updated_by_device_id: string }) {
    return request<PocketList>('/v1/lists', { method: 'POST', body: payload })
  },
  updateList(id: string, payload: Partial<PocketList> & { updated_at: string; updated_by_device_id: string }) {
    return request<PocketList>(`/v1/lists/${id}`, { method: 'PATCH', body: payload })
  },
  deleteList(id: string, payload: { updated_at: string; updated_by_device_id: string }) {
    return request<void>(`/v1/lists/${id}`, { method: 'DELETE', body: payload })
  },

  // Devices / pairing
  listDevices() {
    return request<{ devices: Device[] }>('/v1/devices')
  },
  getDevice(id: string) {
    return request<Device>(`/v1/devices/${id}`)
  },
  updateDevice(
    id: string,
    payload: { device_name?: string; lock_message?: string; volume_percent?: number; brightness_percent?: number },
  ) {
    return request<Device>(`/v1/devices/${id}`, { method: 'PATCH', body: payload })
  },
  unlinkDevice(id: string) {
    return request<void>(`/v1/devices/${id}/link`, { method: 'DELETE' })
  },
  queueDeviceWifi(id: string, payload: { ssid: string; password: string }) {
    return request<{ ok: true; queued: boolean }>(`/v1/devices/${id}/wifi`, {
      method: 'POST',
      body: payload,
    })
  },
  getDeviceParental(id: string) {
    return request<{
      parental: {
        pin_gated_apps?: string[]
        hide_pass_share?: boolean
        block_connectors?: boolean
      }
    }>(`/v1/devices/${id}/parental`)
  },
  updateDeviceParental(
    id: string,
    payload: {
      pin_gated_apps: string[]
      hide_pass_share?: boolean
      block_connectors?: boolean
    },
  ) {
    return request<{
      parental: {
        pin_gated_apps?: string[]
        hide_pass_share?: boolean
        block_connectors?: boolean
      }
    }>(`/v1/devices/${id}/parental`, { method: 'PATCH', body: payload })
  },
  getPairSession(code: string) {
    return request<PairSession>(`/v1/pair/sessions/${encodeURIComponent(code)}`)
  },
  claimPair(code: string) {
    return request<PairClaimResult>('/v1/pair/claim', { method: 'POST', body: { code } })
  },

  // Billing
  createCheckout() {
    return request<{ url: string }>('/v1/billing/checkout', { method: 'POST' })
  },
  createPortal() {
    return request<{ url: string }>('/v1/billing/portal', { method: 'POST' })
  },

  // Connectors
  listConnectors() {
    return request<{ connectors: Connector[] }>('/v1/connectors')
  },
  connectProvider(provider: ConnectorProvider) {
    return request<{ url: string }>(`/v1/connectors/${provider}/connect`, { method: 'POST' })
  },
  disconnectProvider(provider: ConnectorProvider) {
    return request<void>(`/v1/connectors/${provider}`, { method: 'DELETE' })
  },

  // Backup
  listBackups() {
    return request<{ backups: BackupMeta[] }>('/v1/backups')
  },
  createBackup() {
    return request<BackupMeta>('/v1/backups', { method: 'POST' })
  },
  downloadLatestBackup() {
    return request<{ url: string }>('/v1/backups/latest/download')
  },
  restoreBackup(id: string) {
    return request<{ ok: true }>(`/v1/backups/${id}/restore`, { method: 'POST' })
  },

  // Music
  listMusic() {
    return request<{ tracks: MusicTrack[] }>('/v1/music')
  },
  musicLimits() {
    return request<{
      sd_present: boolean
      max_upload_bytes: number
      internal_max_bytes: number
      sd_max_bytes: number
    }>('/v1/music/limits')
  },
  uploadMusic(payload: { title: string; filename: string; mime?: string; audio_b64: string }) {
    return request<{ track: MusicTrack }>('/v1/music', { method: 'POST', body: payload })
  },
  deleteMusic(id: string) {
    return request<void>(`/v1/music/${encodeURIComponent(id)}`, { method: 'DELETE' })
  },

  // Reading — eBooks
  listBooks() {
    return request<{ books: Book[] }>('/v1/books')
  },
  uploadBook(payload: { title: string; author?: string; filename: string; format?: string; file_b64: string }) {
    return request<{ book: Book }>('/v1/books', { method: 'POST', body: payload })
  },
  deleteBook(id: string) {
    return request<void>(`/v1/books/${encodeURIComponent(id)}`, { method: 'DELETE' })
  },

  // Admin (role=admin or ADMIN_EMAILS)
  adminOverview() {
    return request<{
      users: number
      devices: number
      pair_pending: number
      pair_claimed: number
      subscriptions_active: number
      badges: number
      badge_awards: number
    }>('/v1/admin/overview')
  },
  adminListUsers(q?: string, limit = 50) {
    const qs = new URLSearchParams()
    if (q) qs.set('q', q)
    qs.set('limit', String(limit))
    return request<{
      users: Array<{
        id: string
        email: string
        role: string
        email_verified: boolean
        disabled: boolean
        created_at: string
        last_login_at: string | null
        entitlement_status: string
        device_count: number
      }>
    }>(`/v1/admin/users?${qs}`)
  },
  adminPatchUser(id: string, body: Record<string, unknown>) {
    return request<{ user: unknown; entitlement: unknown }>(`/v1/admin/users/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body,
    })
  },
  adminListDevices(userId?: string) {
    const qs = userId ? `?user_id=${encodeURIComponent(userId)}` : ''
    return request<{
      devices: Array<{
        id: string
        device_id: string
        device_name: string
        linked_at: string
        last_seen_at: string | null
        user_id: string
        user_email: string
        volume_percent: number
        brightness_percent: number
        lock_message: string
        parental: { pin_gated_apps?: string[]; hide_pass_share?: boolean; block_connectors?: boolean }
        sd_present: boolean
        pending_wifi_ssid: string | null
        pending_wifi_at: string | null
      }>
    }>(`/v1/admin/devices${qs}`)
  },
  adminPatchDevice(id: string, body: Record<string, unknown>) {
    return request<{ device: unknown }>(`/v1/admin/devices/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body,
    })
  },
  adminUnlinkDevice(id: string) {
    return request<{ ok: true }>(`/v1/admin/devices/${encodeURIComponent(id)}`, { method: 'DELETE' })
  },
  adminForceLinkDevice(body: { device_id: string; user_id: string; device_name?: string }) {
    return request<{ device: unknown }>('/v1/admin/devices/force-link', { method: 'POST', body })
  },
  adminListPairSessions(status?: string) {
    const qs = status ? `?status=${encodeURIComponent(status)}` : ''
    return request<{
      sessions: Array<{
        id: string
        device_id: string
        code_public_hint: string
        status: string
        created_at: string
        expires_at: string
        claimed_at: string | null
        claimed_by_user_id: string | null
      }>
    }>(`/v1/admin/pair-sessions${qs}`)
  },
  adminForceClaimPairSession(id: string, userId: string) {
    return request<{ ok: true }>(`/v1/admin/pair-sessions/${encodeURIComponent(id)}/claim`, {
      method: 'POST',
      body: { user_id: userId },
    })
  },
  adminCancelPairSession(id: string) {
    return request<{ ok: true }>(`/v1/admin/pair-sessions/${encodeURIComponent(id)}`, { method: 'DELETE' })
  },
  adminListBadges() {
    return request<{
      badges: Array<{
        id: string
        key: string
        name: string
        icon_key: string
        description: string
        award_count: number
      }>
      awards: Array<{
        id: string
        badge_id: string
        user_id: string | null
        device_link_id: string | null
        note: string | null
        awarded_at: string
        badge_name: string
        badge_key: string
        icon_key: string
        user_email: string | null
      }>
    }>('/v1/admin/badges')
  },
  adminCreateBadge(body: { name: string; key: string; icon_key?: string; description?: string }) {
    return request<{ badge: unknown }>('/v1/admin/badges', { method: 'POST', body })
  },
  adminAwardBadge(badgeId: string, body: { user_id?: string; device_link_id?: string; note?: string }) {
    return request<{ award: unknown }>(`/v1/admin/badges/${encodeURIComponent(badgeId)}/award`, {
      method: 'POST',
      body,
    })
  },
  adminRevokeAward(id: string) {
    return request<{ ok: true }>(`/v1/admin/badge-awards/${encodeURIComponent(id)}`, { method: 'DELETE' })
  },
  adminActivity() {
    return request<{
      logins: Array<{ id: string; email: string; last_login_at: string }>
      pair_claims: Array<{
        id: string
        device_id: string
        claimed_at: string | null
        user_email: string | null
      }>
      audits: Array<{
        id: string
        action: string
        target_type: string | null
        target_id: string | null
        created_at: string
        actor_email: string | null
        detail: string | null
      }>
    }>('/v1/admin/activity')
  },
  adminStripeStatus() {
    return request<{
      configured: boolean
      mock_mode: boolean
      source: string
      secret_key_set: boolean
      secret_key_masked: string | null
      webhook_secret_set: boolean
      webhook_secret_masked: string | null
      price_monthly_id: string | null
      product_name: string
      webhook_url: string
      updated: Array<{ key: string; updated_at: string; updated_by: string | null }>
    }>('/v1/admin/stripe')
  },
  adminStripeConfigure(body: {
    secret_key?: string
    webhook_secret?: string
    price_monthly_id?: string
    product_name?: string
    clear?: boolean
  }) {
    return request<{
      ok: true
      updates: string[]
      configured: boolean
      mock_mode: boolean
      source: string
      secret_key_masked: string | null
      webhook_secret_masked: string | null
      price_monthly_id: string | null
      product_name: string
    }>('/v1/admin/stripe', { method: 'PUT', body })
  },
}

export function isNetworkError(err: unknown): boolean {
  return err instanceof ApiError && (err.offline || err.status === 0)
}

export function nowIso(): string {
  return new Date().toISOString()
}
