import { useCallback, useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import type { Device } from '../lib/types'
import { relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { WordMark } from '../components/WordMark'
import { useDocumentTitle } from '../components/useDocumentTitle'
import './control.css'

function isOnline(lastSeen: string | null | undefined): boolean {
  if (!lastSeen) return false
  return Date.now() - new Date(lastSeen).getTime() < 1000 * 60 * 15
}

/** Mac / desktop web control — not Companion. Same Pocket Cloud account & pairing. */
export function ControlPage() {
  useDocumentTitle('Control Pocket')
  const { user, isAuthenticated, loading: authLoading, logout } = useAuth()
  const [devices, setDevices] = useState<Device[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  const load = useCallback(async () => {
    setLoading(true)
    setError(null)
    try {
      const res = await api.listDevices()
      setDevices(res.devices)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    if (!isAuthenticated) return
    void load()
  }, [isAuthenticated, load])

  if (authLoading) {
    return (
      <div className="control-shell">
        <p className="muted">Loading…</p>
      </div>
    )
  }

  if (!isAuthenticated) {
    return (
      <div className="control-shell">
        <header className="control-top">
          <WordMark to="/" />
          <Link className="btn btn-primary" to={`/login?return_to=${encodeURIComponent('/control')}`}>
            Sign in
          </Link>
        </header>
        <div className="control-hero stack">
          <p className="eyebrow">Mac control</p>
          <h1>Control Pocket from your Mac</h1>
          <p className="muted control-lede">
            Sign in with the same Pocket Cloud account you use in Companion. Add Wi‑Fi networks, rename
            your device, and tweak settings while Pocket is online — without opening Companion as a
            desktop app.
          </p>
          <div className="actions-row">
            <Link className="btn btn-primary" to={`/login?return_to=${encodeURIComponent('/control')}`}>
              Sign in to control
            </Link>
            <Link className="btn btn-secondary" to="/get-companion">
              Download Companion (phone)
            </Link>
          </div>
        </div>
      </div>
    )
  }

  return (
    <div className="control-shell">
      <header className="control-top">
        <WordMark to="/" />
        <div className="control-top__meta">
          <span className="muted" style={{ fontSize: '0.9rem' }}>
            {user?.email}
          </span>
          <button type="button" className="btn btn-ghost" onClick={() => void logout()}>
            Sign out
          </button>
        </div>
      </header>

      <div className="control-hero stack">
        <p className="eyebrow">Mac control</p>
        <h1>Your Pockets</h1>
        <p className="muted control-lede">
          Remote Wi‑Fi and settings use Pocket Cloud while the device is online. Everyday Notes, Lists,
          Music, and Reading stay in Companion on your phone.
        </p>
        <div className="actions-row">
          <Link className="btn btn-primary" to="/control/link">
            Link a Pocket
          </Link>
          <button type="button" className="btn btn-secondary" disabled={loading} onClick={() => void load()}>
            Refresh
          </button>
        </div>
      </div>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      {loading ? (
        <p className="muted">Loading devices…</p>
      ) : devices.length === 0 ? (
        <div className="panel stack control-panel">
          <h2 style={{ margin: 0, fontSize: '1.15rem' }}>No Pocket linked yet</h2>
          <p className="muted" style={{ margin: 0 }}>
            On the device, finish Wi‑Fi (or skip if already online), open Link, and enter the pairing
            code here. Same account as Companion.
          </p>
          <Link className="btn btn-primary" to="/control/link" style={{ alignSelf: 'flex-start' }}>
            Enter pairing code
          </Link>
        </div>
      ) : (
        <ul className="control-device-list">
          {devices.map((d) => {
            const online = isOnline(d.last_seen_at)
            return (
              <li key={d.id}>
                <Link className="control-device-card" to={`/control/devices/${d.id}`}>
                  <div className="control-device-card__main">
                    <span className="control-device-card__name">{d.device_name || 'Pocket'}</span>
                    <span className={`chip ${online ? 'chip-accent' : ''}`}>
                      {online ? 'Online' : 'Offline'}
                    </span>
                  </div>
                  <span className="muted" style={{ fontSize: '0.9rem' }}>
                    {d.last_seen_at
                      ? `Last seen ${relativeTime(d.last_seen_at)}`
                      : 'Waiting for Pocket to check in over Wi‑Fi'}
                  </span>
                  <span className="control-device-card__cta">Open controls →</span>
                </Link>
              </li>
            )
          })}
        </ul>
      )}

      <p className="footer-note control-footnote">
        Companion remains phone-only.{' '}
        <Link to="/get-companion">Download Companion</Link> for sync and day-to-day use on your phone.
      </p>
    </div>
  )
}
