/** Shared types for Pocket Cloud API v1 (Part E / Part D). */

export type EntitlementStatus = 'free' | 'trialing' | 'active' | 'lapsed' | 'past_due'

export interface User {
  id: string
  email: string
  trial_consumed: boolean
  role?: 'user' | 'admin' | string
  is_admin?: boolean
  email_verified?: boolean
  disabled?: boolean
  last_login_at?: string | null
  created_at?: string
}

export interface Entitlement {
  status: EntitlementStatus
  entitled: boolean
  trial_ends_at?: string | null
  current_period_end?: string | null
  cancel_at_period_end?: boolean
  trial_consumed: boolean
  has_customer: boolean
}

export interface Note {
  id: string
  title: string
  body: string
  created_at: string
  updated_at: string
  updated_by_device_id: string
  deleted_at?: string | null
}

export interface ListItem {
  id: string
  text: string
  checked: boolean
  updated_at: string
}

export interface PocketList {
  id: string
  title: string
  items: ListItem[]
  created_at: string
  updated_at: string
  updated_by_device_id: string
  deleted_at?: string | null
}

export interface Device {
  id: string
  device_id: string
  device_name: string
  linked_at: string
  last_seen_at?: string | null
}

export type PairSessionStatus = 'pending' | 'claimed' | 'expired'

export interface PairSession {
  status: PairSessionStatus
  device_label?: string
}

export interface PairClaimResult {
  device_id: string
  device_name: string
  linked_at: string
}

export type ConnectorProvider = 'drive' | 'dropbox' | 'onedrive'

export interface Connector {
  provider: ConnectorProvider
  status: 'connected' | 'disconnected'
  connected_at?: string | null
}

export interface BackupMeta {
  id: string
  created_at: string
  note_count: number
  list_count: number
  size_bytes?: number
}

export interface MeResponse {
  user: User
  entitlement: Entitlement
}

export class ApiError extends Error {
  status: number
  code?: string
  offline: boolean

  constructor(message: string, status: number, opts?: { code?: string; offline?: boolean }) {
    super(message)
    this.name = 'ApiError'
    this.status = status
    this.code = opts?.code
    this.offline = opts?.offline ?? false
  }
}
