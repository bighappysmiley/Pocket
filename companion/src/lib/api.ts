import { ApiError, type BackupMeta, type Connector, type ConnectorProvider, type Device, type MeResponse, type Note, type PairClaimResult, type PairSession, type PocketList } from './types'

const BUILD_API_BASE = (import.meta.env.VITE_API_BASE as string | undefined)?.replace(/\/$/, '') || ''
const PROD_DEFAULT = 'https://br-super-hill-b40yvyrj-api.compute.c-6.us-east-2.aws.neon.tech'
const STORAGE_KEY = 'pocket_cloud_api_base'

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
  let res: Response
  try {
    res = await fetch(url, {
      method: opts.method ?? (opts.body ? 'POST' : 'GET'),
      credentials: 'include',
      headers: opts.body ? { 'Content-Type': 'application/json', Accept: 'application/json' } : { Accept: 'application/json' },
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
  login(email: string, password: string) {
    return request<{ ok: true; user: { id: string; email: string } }>('/v1/auth/login', {
      method: 'POST',
      body: { email, password },
    })
  },
  requestMagicLink(email: string) {
    return request<{ ok: true }>('/v1/auth/magic-link', { method: 'POST', body: { email } })
  },
  consumeMagicLink(token: string) {
    return request<{ ok: true }>('/v1/auth/callback', { method: 'POST', body: { token } })
  },
  logout() {
    return request<{ ok: true }>('/v1/auth/logout', { method: 'POST' })
  },
  me() {
    return request<MeResponse>('/v1/me')
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
  updateDevice(id: string, payload: { device_name: string }) {
    return request<Device>(`/v1/devices/${id}`, { method: 'PATCH', body: payload })
  },
  unlinkDevice(id: string) {
    return request<void>(`/v1/devices/${id}/link`, { method: 'DELETE' })
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
}

export function isNetworkError(err: unknown): boolean {
  return err instanceof ApiError && (err.offline || err.status === 0)
}

export function nowIso(): string {
  return new Date().toISOString()
}
