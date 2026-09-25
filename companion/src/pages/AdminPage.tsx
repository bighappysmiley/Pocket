import { type FormEvent, useCallback, useEffect, useState } from 'react'
import { Link, Navigate } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ApiError } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'
import { relativeTime } from '../lib/utils'

type Tab = 'overview' | 'users' | 'devices' | 'pairing' | 'badges' | 'activity' | 'stripe'

type Overview = {
  users: number
  devices: number
  pair_pending: number
  pair_claimed: number
  subscriptions_active: number
  badges: number
  badge_awards: number
}

type StripeStatus = {
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
}

type AdminUser = {
  id: string
  email: string
  role: string
  email_verified: boolean
  disabled: boolean
  created_at: string
  last_login_at: string | null
  entitlement_status: string
  device_count: number
}

type AdminDevice = {
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
}

type Badge = {
  id: string
  key: string
  name: string
  icon_key: string
  description: string
  award_count: number
}

type BadgeAward = {
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
}

type PairSessionRow = {
  id: string
  device_id: string
  code_public_hint: string
  status: string
  created_at: string
  expires_at: string
  claimed_at: string | null
  claimed_by_user_id: string | null
}

const TABS: { id: Tab; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'users', label: 'Users' },
  { id: 'devices', label: 'Devices' },
  { id: 'pairing', label: 'Pairing' },
  { id: 'badges', label: 'Badges' },
  { id: 'activity', label: 'Activity' },
  { id: 'stripe', label: 'Stripe' },
]

export function AdminPage() {
  useDocumentTitle('Pocket Cloud Admin')
  const { user, loading: authLoading, isAuthenticated } = useAuth()
  const [tab, setTab] = useState<Tab>('overview')
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const [overview, setOverview] = useState<Overview | null>(null)
  const [users, setUsers] = useState<AdminUser[]>([])
  const [userQ, setUserQ] = useState('')
  const [devices, setDevices] = useState<AdminDevice[]>([])
  const [sessions, setSessions] = useState<PairSessionRow[]>([])
  const [pairFilter, setPairFilter] = useState('')
  const [badges, setBadges] = useState<Badge[]>([])
  const [awards, setAwards] = useState<BadgeAward[]>([])
  const [activity, setActivity] = useState<{
    logins: { id: string; email: string; last_login_at: string }[]
    pair_claims: { id: string; device_id: string; claimed_at: string | null; user_email: string | null }[]
    audits: { id: string; action: string; target_type: string | null; target_id: string | null; created_at: string; actor_email: string | null; detail: string | null }[]
  } | null>(null)
  const [forceLinkForm, setForceLinkForm] = useState({ device_id: '', user_id: '', device_name: '' })
  const [badgeForm, setBadgeForm] = useState({ name: '', key: '', icon_key: 'star', description: '' })
  const [awardForm, setAwardForm] = useState({ badge_id: '', user_id: '', note: '' })
  const [notice, setNotice] = useState<string | null>(null)
  const [stripe, setStripe] = useState<StripeStatus | null>(null)
  const [stripeForm, setStripeForm] = useState({
    secret_key: '',
    webhook_secret: '',
    price_monthly_id: '',
    product_name: 'Pocket Cloud',
  })

  const isAdmin = Boolean(user && (user.is_admin || user.role === 'admin'))

  const fail = (err: unknown) => {
    if (err instanceof ApiError && err.status === 403) setError('Admin access required.')
    else if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
    else if (err instanceof ApiError) setError(err.message)
    else setError('Something went wrong.')
  }

  const loadOverview = useCallback(async () => {
    const data = await api.adminOverview()
    setOverview(data)
  }, [])

  const loadUsers = useCallback(async (q = userQ) => {
    const data = await api.adminListUsers(q)
    setUsers(data.users)
  }, [userQ])

  const loadDevices = useCallback(async () => {
    const data = await api.adminListDevices()
    setDevices(data.devices)
  }, [])

  const loadPairing = useCallback(async (status = pairFilter) => {
    const data = await api.adminListPairSessions(status || undefined)
    setSessions(data.sessions)
  }, [pairFilter])

  const loadBadges = useCallback(async () => {
    const data = await api.adminListBadges()
    setBadges(data.badges)
    setAwards(data.awards)
    if (!awardForm.badge_id && data.badges[0]) {
      setAwardForm((f) => ({ ...f, badge_id: data.badges[0]!.id }))
    }
  }, [awardForm.badge_id])

  const loadActivity = useCallback(async () => {
    const data = await api.adminActivity()
    setActivity(data)
  }, [])

  const loadStripe = useCallback(async () => {
    const data = await api.adminStripeStatus()
    setStripe(data)
    setStripeForm((f) => ({
      ...f,
      price_monthly_id: data.price_monthly_id || '',
      product_name: data.product_name || 'Pocket Cloud',
      secret_key: '',
      webhook_secret: '',
    }))
  }, [])

  const refresh = useCallback(async () => {
    setError(null)
    setBusy(true)
    try {
      if (tab === 'overview') await loadOverview()
      else if (tab === 'users') await loadUsers()
      else if (tab === 'devices') await loadDevices()
      else if (tab === 'pairing') await loadPairing()
      else if (tab === 'badges') await loadBadges()
      else if (tab === 'activity') await loadActivity()
      else if (tab === 'stripe') await loadStripe()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }, [tab, loadOverview, loadUsers, loadDevices, loadPairing, loadBadges, loadActivity, loadStripe])

  useEffect(() => {
    if (!authLoading && isAuthenticated && isAdmin) void refresh()
  }, [authLoading, isAuthenticated, isAdmin, refresh])

  if (authLoading) {
    return (
      <div className="page">
        <p className="muted">Loading…</p>
      </div>
    )
  }

  if (!isAuthenticated) {
    return <Navigate to={`/login?return_to=${encodeURIComponent('/admin/')}`} replace />
  }

  if (!isAdmin) {
    return (
      <div className="page stack">
        <h1>Pocket Cloud Admin</h1>
        <p className="muted">This area is only for Pocket Cloud admins.</p>
        <Link className="btn btn-secondary" to="/">
          Back home
        </Link>
      </div>
    )
  }

  async function patchUser(id: string, body: Record<string, unknown>, okMsg: string) {
    setBusy(true)
    setNotice(null)
    setError(null)
    try {
      await api.adminPatchUser(id, body)
      setNotice(okMsg)
      await loadUsers()
      if (tab === 'overview') await loadOverview()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function renameDevice(id: string, current: string) {
    const name = window.prompt('Device label', current)
    if (name == null || !name.trim()) return
    setBusy(true)
    setError(null)
    try {
      await api.adminPatchDevice(id, { device_name: name.trim() })
      setNotice('Device renamed.')
      await loadDevices()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function unlinkDevice(id: string, label: string) {
    if (!window.confirm(`Unlink “${label}”? The device will need to pair again.`)) return
    setBusy(true)
    setError(null)
    try {
      await api.adminUnlinkDevice(id)
      setNotice('Device unlinked.')
      await loadDevices()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function patchDevice(id: string, body: Record<string, unknown>, okMsg: string) {
    setBusy(true)
    setError(null)
    try {
      await api.adminPatchDevice(id, body)
      setNotice(okMsg)
      await loadDevices()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function setDeviceVolume(d: AdminDevice) {
    const raw = window.prompt('Volume percent (0–100)', String(d.volume_percent))
    if (raw == null) return
    const v = Number(raw)
    if (!Number.isFinite(v)) return
    await patchDevice(d.id, { volume_percent: v }, 'Volume pushed to device.')
  }

  async function setDeviceBrightness(d: AdminDevice) {
    const raw = window.prompt('Brightness percent (0–100)', String(d.brightness_percent))
    if (raw == null) return
    const v = Number(raw)
    if (!Number.isFinite(v)) return
    await patchDevice(d.id, { brightness_percent: v }, 'Brightness pushed to device.')
  }

  async function setLockMessage(d: AdminDevice) {
    const msg = window.prompt('Lock screen message (up to 40 characters)', d.lock_message)
    if (msg == null) return
    await patchDevice(d.id, { lock_message: msg }, 'Lock message pushed to device.')
  }

  async function pushWifi(d: AdminDevice) {
    const ssid = window.prompt('Wi-Fi network name to push to this Pocket')
    if (ssid == null || !ssid.trim()) return
    const password = window.prompt('Wi-Fi password (leave blank for open networks)') || ''
    await patchDevice(d.id, { ssid: ssid.trim(), password }, 'Wi-Fi credentials queued for next check-in.')
  }

  async function toggleParentalLock(d: AdminDevice) {
    const hasLock = Boolean(
      d.parental?.hide_pass_share || d.parental?.block_connectors || (d.parental?.pin_gated_apps?.length ?? 0) > 0,
    )
    if (!hasLock) {
      setNotice('No parental locks are set on this device.')
      return
    }
    if (!window.confirm('Unlock/disable all parental restrictions on this device?')) return
    await patchDevice(d.id, { unlock_parental: true }, 'Parental locks cleared.')
  }

  async function toggleSdFlag(d: AdminDevice) {
    await patchDevice(d.id, { sd_present: !d.sd_present }, d.sd_present ? 'Marked microSD as absent.' : 'Marked microSD as present.')
  }

  async function resetDeviceSettings(d: AdminDevice) {
    if (!window.confirm(`Wipe/reset all settings on “${d.device_name}” (volume, brightness, lock message, parental locks, queued Wi‑Fi)? This does not unpair it.`)) return
    await patchDevice(d.id, { reset: true }, 'Device settings wiped to defaults.')
  }

  async function forceReassignDevice(d: AdminDevice) {
    const userId = window.prompt('Force-pair: move this device to a different user id', d.user_id)
    if (userId == null || !userId.trim() || userId.trim() === d.user_id) return
    await patchDevice(d.id, { user_id: userId.trim() }, 'Device force-paired to that user.')
  }

  async function onForceLinkDevice(e: FormEvent) {
    e.preventDefault()
    if (!forceLinkForm.device_id.trim() || !forceLinkForm.user_id.trim()) {
      setError('Enter both a device id and a user id.')
      return
    }
    setBusy(true)
    setError(null)
    try {
      await api.adminForceLinkDevice({
        device_id: forceLinkForm.device_id.trim(),
        user_id: forceLinkForm.user_id.trim(),
        device_name: forceLinkForm.device_name.trim() || undefined,
      })
      setNotice('Device force-linked to that user — it will pick up the account on its next check-in.')
      setForceLinkForm({ device_id: '', user_id: '', device_name: '' })
      await loadDevices()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function forceClaimPairSession(id: string) {
    const userId = window.prompt('Force-claim this pairing code for user id:')
    if (userId == null || !userId.trim()) return
    setBusy(true)
    setError(null)
    try {
      await api.adminForceClaimPairSession(id, userId.trim())
      setNotice('Pairing code force-claimed for that user.')
      await loadPairing()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function cancelPairSession(id: string) {
    if (!window.confirm('Cancel/expire this pairing code?')) return
    setBusy(true)
    setError(null)
    try {
      await api.adminCancelPairSession(id)
      setNotice('Pairing code canceled.')
      await loadPairing()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function onCreateBadge(e: FormEvent) {
    e.preventDefault()
    setBusy(true)
    setError(null)
    try {
      await api.adminCreateBadge(badgeForm)
      setBadgeForm({ name: '', key: '', icon_key: 'star', description: '' })
      setNotice('Badge created.')
      await loadBadges()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function onAwardBadge(e: FormEvent) {
    e.preventDefault()
    if (!awardForm.badge_id || !awardForm.user_id.trim()) {
      setError('Pick a badge and enter a user id.')
      return
    }
    setBusy(true)
    setError(null)
    try {
      await api.adminAwardBadge(awardForm.badge_id, {
        user_id: awardForm.user_id.trim(),
        note: awardForm.note.trim() || undefined,
      })
      setAwardForm((f) => ({ ...f, user_id: '', note: '' }))
      setNotice('Badge awarded.')
      await loadBadges()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function revokeAward(id: string) {
    if (!window.confirm('Revoke this badge award?')) return
    setBusy(true)
    setError(null)
    try {
      await api.adminRevokeAward(id)
      setNotice('Award revoked.')
      await loadBadges()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function onSaveStripe(e: FormEvent) {
    e.preventDefault()
    setBusy(true)
    setError(null)
    setNotice(null)
    try {
      const body: {
        secret_key?: string
        webhook_secret?: string
        price_monthly_id?: string
        product_name?: string
      } = {}
      if (stripeForm.secret_key.trim()) body.secret_key = stripeForm.secret_key.trim()
      if (stripeForm.webhook_secret.trim()) body.webhook_secret = stripeForm.webhook_secret.trim()
      if (stripeForm.price_monthly_id.trim() || stripe?.price_monthly_id) {
        body.price_monthly_id = stripeForm.price_monthly_id.trim()
      }
      if (stripeForm.product_name.trim()) body.product_name = stripeForm.product_name.trim()
      if (!body.secret_key && !body.webhook_secret && body.price_monthly_id === undefined && !body.product_name) {
        setError('Paste a Stripe secret key, webhook secret, or price id to save.')
        setBusy(false)
        return
      }
      await api.adminStripeConfigure(body)
      setNotice('Stripe settings saved. Keys stay on the server — never paste them in chat.')
      await loadStripe()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  async function onClearStripe() {
    if (!window.confirm('Clear Admin-stored Stripe keys? Billing will fall back to env vars or mock mode.')) return
    setBusy(true)
    setError(null)
    try {
      await api.adminStripeConfigure({ clear: true })
      setNotice('Cleared Admin Stripe settings.')
      await loadStripe()
    } catch (err) {
      fail(err)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="page page--wide stack admin-page">
      <div className="stack-sm">
        <p className="eyebrow">Pocket Cloud</p>
        <h1>Admin</h1>
        <p className="muted">Users, devices, pairing, badges, Stripe, and subscriptions for ops.</p>
      </div>

      <div className="admin-tabs" role="tablist" aria-label="Admin sections">
        {TABS.map((t) => (
          <button
            key={t.id}
            type="button"
            role="tab"
            aria-selected={tab === t.id}
            className={tab === t.id ? 'admin-tab active' : 'admin-tab'}
            onClick={() => {
              setNotice(null)
              setTab(t.id)
            }}
          >
            {t.label}
          </button>
        ))}
      </div>

      {error ? <ErrorState message={error} onRetry={() => void refresh()} /> : null}
      {notice ? (
        <div className="panel" role="status">
          <p>{notice}</p>
        </div>
      ) : null}
      {busy && !overview && tab === 'overview' ? <p className="muted">Loading…</p> : null}

      {tab === 'overview' && overview ? (
        <div className="admin-stats">
          {(
            [
              ['Users', overview.users],
              ['Devices', overview.devices],
              ['Pending pairs', overview.pair_pending],
              ['Claimed pairs', overview.pair_claimed],
              ['Active Cloud', overview.subscriptions_active],
              ['Badges', overview.badges],
              ['Awards', overview.badge_awards],
            ] as const
          ).map(([label, value]) => (
            <div key={label} className="admin-stat">
              <div className="admin-stat-value">{value}</div>
              <div className="admin-stat-label">{label}</div>
            </div>
          ))}
        </div>
      ) : null}

      {tab === 'users' ? (
        <div className="stack">
          <form
            className="row-between"
            style={{ gap: '0.75rem', flexWrap: 'wrap' }}
            onSubmit={(e) => {
              e.preventDefault()
              void loadUsers(userQ).catch(fail)
            }}
          >
            <input
              className="input"
              style={{ flex: '1 1 14rem' }}
              placeholder="Search email or user id"
              value={userQ}
              onChange={(e) => setUserQ(e.target.value)}
            />
            <button type="submit" className="btn btn-secondary" disabled={busy}>
              Search
            </button>
          </form>
          <ul className="list admin-list">
            {users.map((u) => (
              <li key={u.id} className="panel stack-sm">
                <div className="row-between" style={{ gap: '0.5rem', flexWrap: 'wrap' }}>
                  <div>
                    <div className="list-title">{u.email}</div>
                    <div className="list-meta">
                      {u.role} · {u.entitlement_status}
                      {u.disabled ? ' · disabled' : ''}
                      {u.email_verified ? '' : ' · unverified'} · {u.device_count} device
                      {u.device_count === 1 ? '' : 's'}
                      {u.last_login_at ? ` · last login ${relativeTime(u.last_login_at)}` : ''}
                    </div>
                  </div>
                </div>
                <div className="actions-row" style={{ flexWrap: 'wrap' }}>
                  {!u.email_verified ? (
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void patchUser(u.id, { verify_email: true }, 'Email verified.')}>
                      Verify email
                    </button>
                  ) : null}
                  <button
                    type="button"
                    className="btn btn-ghost"
                    disabled={busy}
                    onClick={() => void patchUser(u.id, { disabled: !u.disabled }, u.disabled ? 'User enabled.' : 'User disabled.')}
                  >
                    {u.disabled ? 'Enable' : 'Disable'}
                  </button>
                  <button
                    type="button"
                    className="btn btn-ghost"
                    disabled={busy || u.id === user?.id}
                    onClick={() =>
                      void patchUser(u.id, { role: u.role === 'admin' ? 'user' : 'admin' }, u.role === 'admin' ? 'Role set to user.' : 'Role set to admin.')
                    }
                  >
                    {u.role === 'admin' ? 'Make user' : 'Make admin'}
                  </button>
                  <button
                    type="button"
                    className="btn btn-ghost"
                    disabled={busy}
                    onClick={() => void patchUser(u.id, { entitlement_status: 'trialing' }, 'Trial granted (7 days).')}
                  >
                    Grant trial
                  </button>
                  <button
                    type="button"
                    className="btn btn-ghost"
                    disabled={busy}
                    onClick={() =>
                      void patchUser(u.id, { entitlement_status: 'active' }, 'Permanent Pocket Cloud granted.')
                    }
                  >
                    Grant permanent Cloud
                  </button>
                  <button
                    type="button"
                    className="btn btn-ghost"
                    disabled={busy}
                    onClick={() => void patchUser(u.id, { entitlement_status: 'free' }, 'Entitlement cleared.')}
                  >
                    Clear Cloud
                  </button>
                </div>
                <p className="muted" style={{ fontSize: '0.85rem' }}>
                  Permanent = active with no end date. Trial = 7 days. id <code>{u.id}</code>
                </p>
              </li>
            ))}
          </ul>
          {users.length === 0 && !busy ? <p className="muted">No users match.</p> : null}
        </div>
      ) : null}

      {tab === 'devices' ? (
        <div className="stack">
          <form className="panel stack" onSubmit={(e) => void onForceLinkDevice(e)}>
            <h2>Force-pair a device</h2>
            <p className="muted">
              Attach a Pocket directly to a user's account by device id — bypasses the phone QR/claim flow entirely.
              Useful while updates can't ship: the firmware only needs the shared device key + device id, so this
              takes effect on the device's next check-in.
            </p>
            <div className="field">
              <label htmlFor="force-device-id">Device id</label>
              <input
                id="force-device-id"
                className="input"
                value={forceLinkForm.device_id}
                onChange={(e) => setForceLinkForm((f) => ({ ...f, device_id: e.target.value }))}
                placeholder="Device's device_id (from firmware serial log or a stuck pairing session)"
                required
              />
            </div>
            <div className="field">
              <label htmlFor="force-user-id">User id</label>
              <input
                id="force-user-id"
                className="input"
                value={forceLinkForm.user_id}
                onChange={(e) => setForceLinkForm((f) => ({ ...f, user_id: e.target.value }))}
                placeholder="Paste user id from Users tab"
                required
              />
            </div>
            <div className="field">
              <label htmlFor="force-device-name">Label (optional)</label>
              <input
                id="force-device-name"
                className="input"
                value={forceLinkForm.device_name}
                onChange={(e) => setForceLinkForm((f) => ({ ...f, device_name: e.target.value }))}
                placeholder="Pocket"
              />
            </div>
            <button type="submit" className="btn btn-primary" disabled={busy}>
              Force-pair
            </button>
          </form>

          <ul className="list admin-list">
            {devices.map((d) => {
              const hasLock = Boolean(
                d.parental?.hide_pass_share || d.parental?.block_connectors || (d.parental?.pin_gated_apps?.length ?? 0) > 0,
              )
              return (
                <li key={d.id} className="panel stack-sm">
                  <div className="list-title">{d.device_name || 'Pocket'}</div>
                  <div className="list-meta">
                    {d.user_email} · linked {relativeTime(d.linked_at)}
                    {d.last_seen_at ? ` · seen ${relativeTime(d.last_seen_at)}` : ''}
                  </div>
                  <div className="list-meta">
                    volume {d.volume_percent}% · brightness {d.brightness_percent}% · microSD{' '}
                    {d.sd_present ? 'reported present' : 'not reported'}
                    {hasLock ? ' · parental locks ON' : ''}
                    {d.lock_message ? ` · lock message “${d.lock_message}”` : ''}
                    {d.pending_wifi_ssid ? ` · Wi‑Fi “${d.pending_wifi_ssid}” queued` : ''}
                  </div>
                  <div className="actions-row" style={{ flexWrap: 'wrap' }}>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void renameDevice(d.id, d.device_name)}>
                      Rename
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void setDeviceVolume(d)}>
                      Push volume
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void setDeviceBrightness(d)}>
                      Push brightness
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void setLockMessage(d)}>
                      Set lock message
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void pushWifi(d)}>
                      Push Wi‑Fi
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy || !hasLock} onClick={() => void toggleParentalLock(d)}>
                      Unlock parental
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void toggleSdFlag(d)}>
                      {d.sd_present ? 'Clear SD flag' : 'Mark SD present'}
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void forceReassignDevice(d)}>
                      Force-pair to user…
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void resetDeviceSettings(d)}>
                      Wipe/reset settings
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void unlinkDevice(d.id, d.device_name)}>
                      Force-unpair
                    </button>
                  </div>
                  <p className="muted" style={{ fontSize: '0.85rem' }}>
                    device <code>{d.device_id}</code> · id <code>{d.id}</code>
                  </p>
                </li>
              )
            })}
            {devices.length === 0 && !busy ? <p className="muted">No linked devices.</p> : null}
          </ul>
        </div>
      ) : null}

      {tab === 'pairing' ? (
        <div className="stack">
          <div className="actions-row">
            {(['', 'pending', 'claimed', 'expired'] as const).map((s) => (
              <button
                key={s || 'all'}
                type="button"
                className={pairFilter === s ? 'btn btn-secondary' : 'btn btn-ghost'}
                onClick={() => {
                  setPairFilter(s)
                  void loadPairing(s).catch(fail)
                }}
              >
                {s || 'All'}
              </button>
            ))}
          </div>
          <ul className="list admin-list">
            {sessions.map((s) => (
              <li key={s.id} className="panel stack-sm">
                <div className="list-title">
                  {s.status} · {s.code_public_hint}
                </div>
                <div className="list-meta">
                  device {s.device_id} · created {relativeTime(s.created_at)}
                  {s.claimed_at ? ` · claimed ${relativeTime(s.claimed_at)}` : ''}
                </div>
                {s.status === 'pending' ? (
                  <div className="actions-row">
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void forceClaimPairSession(s.id)}>
                      Force-claim for user…
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void cancelPairSession(s.id)}>
                      Cancel code
                    </button>
                  </div>
                ) : null}
              </li>
            ))}
          </ul>
          {sessions.length === 0 && !busy ? <p className="muted">No pairing sessions.</p> : null}
        </div>
      ) : null}

      {tab === 'badges' ? (
        <div className="stack">
          <form className="panel stack" onSubmit={(e) => void onCreateBadge(e)}>
            <h2>Create badge</h2>
            <div className="field">
              <label htmlFor="badge-name">Name</label>
              <input id="badge-name" className="input" value={badgeForm.name} onChange={(e) => setBadgeForm({ ...badgeForm, name: e.target.value })} required />
            </div>
            <div className="field">
              <label htmlFor="badge-key">Key</label>
              <input id="badge-key" className="input" value={badgeForm.key} onChange={(e) => setBadgeForm({ ...badgeForm, key: e.target.value })} placeholder="unique_key" required />
            </div>
            <div className="field">
              <label htmlFor="badge-icon">Icon key</label>
              <input id="badge-icon" className="input" value={badgeForm.icon_key} onChange={(e) => setBadgeForm({ ...badgeForm, icon_key: e.target.value })} />
            </div>
            <div className="field">
              <label htmlFor="badge-desc">Description</label>
              <input id="badge-desc" className="input" value={badgeForm.description} onChange={(e) => setBadgeForm({ ...badgeForm, description: e.target.value })} />
            </div>
            <button type="submit" className="btn btn-primary" disabled={busy}>
              Create
            </button>
          </form>

          <form className="panel stack" onSubmit={(e) => void onAwardBadge(e)}>
            <h2>Award badge</h2>
            <div className="field">
              <label htmlFor="award-badge">Badge</label>
              <select
                id="award-badge"
                className="input"
                value={awardForm.badge_id}
                onChange={(e) => setAwardForm({ ...awardForm, badge_id: e.target.value })}
              >
                {badges.map((b) => (
                  <option key={b.id} value={b.id}>
                    {b.name}
                  </option>
                ))}
              </select>
            </div>
            <div className="field">
              <label htmlFor="award-user">User id</label>
              <input
                id="award-user"
                className="input"
                value={awardForm.user_id}
                onChange={(e) => setAwardForm({ ...awardForm, user_id: e.target.value })}
                placeholder="Paste user id from Users tab"
                required
              />
            </div>
            <div className="field">
              <label htmlFor="award-note">Note (optional)</label>
              <input id="award-note" className="input" value={awardForm.note} onChange={(e) => setAwardForm({ ...awardForm, note: e.target.value })} />
            </div>
            <button type="submit" className="btn btn-primary" disabled={busy}>
              Award
            </button>
          </form>

          <h2>Catalog</h2>
          <ul className="list admin-list">
            {badges.map((b) => (
              <li key={b.id} className="panel stack-sm">
                <div className="list-title">
                  {b.name} <span className="muted">({b.key})</span>
                </div>
                <div className="list-meta">
                  icon {b.icon_key} · {b.award_count} award{b.award_count === 1 ? '' : 's'}
                </div>
                {b.description ? <p className="muted">{b.description}</p> : null}
              </li>
            ))}
          </ul>

          <h2>Recent awards</h2>
          <ul className="list admin-list">
            {awards.map((a) => (
              <li key={a.id} className="panel stack-sm">
                <div className="list-title">
                  {a.badge_name} → {a.user_email || a.device_link_id || '—'}
                </div>
                <div className="list-meta">{relativeTime(a.awarded_at)}{a.note ? ` · ${a.note}` : ''}</div>
                <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void revokeAward(a.id)}>
                  Revoke
                </button>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      {tab === 'activity' && activity ? (
        <div className="stack">
          <section className="stack-sm">
            <h2>Recent logins</h2>
            <ul className="list">
              {activity.logins.map((l) => (
                <li key={l.id} className="list-link" style={{ pointerEvents: 'none' }}>
                  <div className="list-title">{l.email}</div>
                  <div className="list-meta">{relativeTime(l.last_login_at)}</div>
                </li>
              ))}
              {activity.logins.length === 0 ? <p className="muted">No logins recorded yet.</p> : null}
            </ul>
          </section>
          <section className="stack-sm">
            <h2>Pair claims</h2>
            <ul className="list">
              {activity.pair_claims.map((c) => (
                <li key={c.id} className="list-link" style={{ pointerEvents: 'none' }}>
                  <div className="list-title">{c.user_email || 'Unknown user'}</div>
                  <div className="list-meta">
                    {c.device_id}
                    {c.claimed_at ? ` · ${relativeTime(c.claimed_at)}` : ''}
                  </div>
                </li>
              ))}
            </ul>
          </section>
          <section className="stack-sm">
            <h2>Admin actions</h2>
            <ul className="list">
              {activity.audits.map((a) => (
                <li key={a.id} className="list-link" style={{ pointerEvents: 'none' }}>
                  <div className="list-title">
                    {a.action}
                    {a.actor_email ? ` · ${a.actor_email}` : ''}
                  </div>
                  <div className="list-meta">
                    {a.target_type} {a.target_id} · {relativeTime(a.created_at)}
                  </div>
                </li>
              ))}
            </ul>
          </section>
        </div>
      ) : null}

      {tab === 'stripe' ? (
        <div className="stack">
          <section className="panel stack">
            <h2>Stripe billing</h2>
            <p className="muted">
              Paste keys from the Stripe Dashboard here. They are stored server-side for Checkout and webhooks —
              do not put them in chat or commit them.
            </p>
            {stripe ? (
              <div className="stack-sm">
                <p>
                  Status:{' '}
                  <strong>{stripe.mock_mode ? 'Mock billing (no secret key)' : 'Live Stripe'}</strong>
                  {stripe.source !== 'none' ? ` · source: ${stripe.source}` : ''}
                </p>
                <p className="muted">
                  Secret: {stripe.secret_key_masked || 'not set'}
                  {' · '}
                  Webhook: {stripe.webhook_secret_masked || 'not set'}
                </p>
                <p className="muted">
                  Webhook URL: <code>{stripe.webhook_url}</code>
                </p>
              </div>
            ) : busy ? (
              <p className="muted">Loading…</p>
            ) : null}
          </section>

          <form className="panel stack" onSubmit={(e) => void onSaveStripe(e)}>
            <label className="field">
              <span>Secret key</span>
              <input
                type="password"
                autoComplete="off"
                placeholder={stripe?.secret_key_set ? '•••• leave blank to keep' : 'sk_test_… or sk_live_…'}
                value={stripeForm.secret_key}
                onChange={(e) => setStripeForm((f) => ({ ...f, secret_key: e.target.value }))}
              />
            </label>
            <label className="field">
              <span>Webhook signing secret</span>
              <input
                type="password"
                autoComplete="off"
                placeholder={stripe?.webhook_secret_set ? '•••• leave blank to keep' : 'whsec_…'}
                value={stripeForm.webhook_secret}
                onChange={(e) => setStripeForm((f) => ({ ...f, webhook_secret: e.target.value }))}
              />
            </label>
            <label className="field">
              <span>Monthly price id</span>
              <input
                type="text"
                autoComplete="off"
                placeholder="price_…"
                value={stripeForm.price_monthly_id}
                onChange={(e) => setStripeForm((f) => ({ ...f, price_monthly_id: e.target.value }))}
              />
            </label>
            <label className="field">
              <span>Product name</span>
              <input
                type="text"
                autoComplete="off"
                value={stripeForm.product_name}
                onChange={(e) => setStripeForm((f) => ({ ...f, product_name: e.target.value }))}
              />
            </label>
            <div className="actions">
              <button type="submit" className="btn btn-primary" disabled={busy}>
                Save Stripe settings
              </button>
              <button type="button" className="btn btn-ghost" disabled={busy} onClick={() => void onClearStripe()}>
                Clear saved keys
              </button>
            </div>
          </form>
        </div>
      ) : null}
    </div>
  )
}
