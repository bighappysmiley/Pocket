import { type FormEvent, useEffect, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import type { Device } from '../lib/types'
import { relativeTime, sanitizeDeviceName, validateDeviceName } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

const PARENTAL_APPS: { id: string; label: string }[] = [
  { id: 'notes', label: 'Notes' },
  { id: 'ledger', label: 'Ledger' },
  { id: 'clock', label: 'Clock' },
  { id: 'pass', label: 'Pass' },
  { id: 'weather', label: 'Weather' },
  { id: 'music', label: 'Music' },
]

function isOnline(lastSeen: string | null | undefined): boolean {
  if (!lastSeen) return false
  return Date.now() - new Date(lastSeen).getTime() < 1000 * 60 * 15
}

export function DeviceDetailPage() {
  const { id = '' } = useParams()
  const navigate = useNavigate()
  const { isEntitled } = useAuth()
  const [device, setDevice] = useState<Device | null>(null)
  const [name, setName] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [msg, setMsg] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const [confirmUnlink, setConfirmUnlink] = useState(false)
  const [gated, setGated] = useState<string[]>([])
  const [hidePassShare, setHidePassShare] = useState(false)
  const [blockConnectors, setBlockConnectors] = useState(false)
  const [wifiSsid, setWifiSsid] = useState('')
  const [wifiPassword, setWifiPassword] = useState('')

  useDocumentTitle(device?.device_name || 'Pocket Classic')

  async function load() {
    setError(null)
    try {
      const d = await api.getDevice(id)
      setDevice(d)
      setName(d.device_name || 'Pocket')
      try {
        const p = await api.getDeviceParental(id)
        setGated(p.parental?.pin_gated_apps || [])
        setHidePassShare(Boolean(p.parental?.hide_pass_share))
        setBlockConnectors(Boolean(p.parental?.block_connectors))
      } catch {
        /* parental optional if older API */
      }
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  useEffect(() => {
    void load()
  }, [id])

  async function onSave(e: FormEvent) {
    e.preventDefault()
    const validation = validateDeviceName(name)
    if (validation) {
      setError(validation)
      return
    }
    setBusy(true)
    setError(null)
    setMsg(null)
    try {
      const d = await api.updateDevice(id, { device_name: name.trim() })
      setDevice({ ...d, last_seen_at: d.last_seen_at ?? device?.last_seen_at ?? null })
      setMsg('Name saved. Pocket picks up the new name on its next check-in.')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setBusy(false)
    }
  }

  async function onSaveParental(e: FormEvent) {
    e.preventDefault()
    setBusy(true)
    setError(null)
    setMsg(null)
    try {
      await api.updateDeviceParental(id, {
        pin_gated_apps: gated,
        hide_pass_share: hidePassShare,
        block_connectors: blockConnectors,
      })
      setMsg('Parental controls saved. Pocket applies them when online.')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Could not save parental controls.')
    } finally {
      setBusy(false)
    }
  }

  async function onQueueWifi(e: FormEvent) {
    e.preventDefault()
    if (!wifiSsid.trim()) {
      setError('Enter a network name.')
      return
    }
    setBusy(true)
    setError(null)
    setMsg(null)
    try {
      await api.queueDeviceWifi(id, { ssid: wifiSsid.trim(), password: wifiPassword })
      setMsg('Wi‑Fi queued. Pocket applies it within about a minute while online — no SoftAP hop.')
      setWifiSsid('')
      setWifiPassword('')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Could not queue Wi‑Fi.')
    } finally {
      setBusy(false)
    }
  }

  async function onUnlink() {
    setBusy(true)
    setError(null)
    try {
      await api.unlinkDevice(id)
      setMsg('Device unlinked.')
      navigate('/devices', { replace: true })
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setBusy(false)
      setConfirmUnlink(false)
    }
  }

  const online = device ? isOnline(device.last_seen_at) : false
  const statusLabel = !device
    ? ''
    : [
        'Linked',
        online ? 'Online' : 'Offline',
        isEntitled ? 'Cloud active' : 'Cloud not active',
      ].join(' · ')

  return (
    <div className="page stack">
      <div className="row-between">
        <h1>{device?.device_name || 'Pocket'}</h1>
        <Link to="/devices" className="btn btn-ghost">
          Back
        </Link>
      </div>

      {error ? (
        error.includes('unreachable') ? (
          <ErrorState message={error} onRetry={() => void load()} />
        ) : (
          <p role="alert" className="muted">
            {error}
          </p>
        )
      ) : null}
      {msg ? <p className="muted">{msg}</p> : null}

      {!device ? (
        <p className="muted">Loading…</p>
      ) : (
        <>
          <p className="chip">{statusLabel}</p>
          {device.last_seen_at ? (
            <p className="muted">Last seen {relativeTime(device.last_seen_at)}</p>
          ) : (
            <p className="muted">Waiting for Pocket to check in over Wi‑Fi.</p>
          )}

          <form className="panel stack" onSubmit={(e) => void onSave(e)}>
            <div className="field">
              <label htmlFor="device_name">Device name</label>
              <input
                id="device_name"
                value={name}
                maxLength={20}
                onChange={(e) => setName(sanitizeDeviceName(e.target.value))}
              />
              <span className="muted" style={{ fontSize: '0.8rem' }}>
                {name.length}/20
              </span>
            </div>
            <button type="submit" className="btn btn-primary" disabled={busy}>
              Save name
            </button>
          </form>

          <form className="panel stack" onSubmit={(e) => void onQueueWifi(e)}>
            <h2 style={{ margin: 0, fontSize: '1.05rem' }}>Add Wi‑Fi in Companion</h2>
            <p className="muted" style={{ margin: 0 }}>
              Pocket Classic must already be online. We send the network through Pocket Cloud — no join
              SoftAP / no 192.168.4.1.
            </p>
            <div className="field">
              <label htmlFor="wifi_ssid">Network name</label>
              <input
                id="wifi_ssid"
                value={wifiSsid}
                onChange={(e) => setWifiSsid(e.target.value)}
                placeholder="Home or hotspot name"
              />
            </div>
            <div className="field">
              <label htmlFor="wifi_password">Password</label>
              <input
                id="wifi_password"
                type="password"
                value={wifiPassword}
                onChange={(e) => setWifiPassword(e.target.value)}
              />
            </div>
            <button type="submit" className="btn btn-primary btn-block" disabled={busy}>
              Send to Pocket
            </button>
            <Link className="btn btn-block" to={`/link?add=1&device=${encodeURIComponent(id)}`}>
              Open full Link flow
            </Link>
          </form>

          <form className="panel stack" onSubmit={(e) => void onSaveParental(e)}>
            <h2 style={{ margin: 0, fontSize: '1.05rem' }}>Parental controls</h2>
            <p className="muted" style={{ margin: 0 }}>
              Require the on-device PIN before opening selected apps. The PIN never leaves Pocket.
            </p>
            {PARENTAL_APPS.map((app) => (
              <label key={app.id} style={{ display: 'flex', gap: '0.5rem', alignItems: 'center' }}>
                <input
                  type="checkbox"
                  checked={gated.includes(app.id)}
                  onChange={(e) => {
                    setGated((prev) =>
                      e.target.checked ? [...prev, app.id] : prev.filter((x) => x !== app.id),
                    )
                  }}
                />
                Require PIN for {app.label}
              </label>
            ))}
            <label style={{ display: 'flex', gap: '0.5rem', alignItems: 'center' }}>
              <input
                type="checkbox"
                checked={hidePassShare}
                onChange={(e) => setHidePassShare(e.target.checked)}
              />
              Hide Pass share actions
            </label>
            <label style={{ display: 'flex', gap: '0.5rem', alignItems: 'center' }}>
              <input
                type="checkbox"
                checked={blockConnectors}
                onChange={(e) => setBlockConnectors(e.target.checked)}
              />
              Block connectors in Companion
            </label>
            <button type="submit" className="btn btn-primary" disabled={busy}>
              Save parental controls
            </button>
          </form>

          {!confirmUnlink ? (
            <button type="button" className="btn btn-danger" onClick={() => setConfirmUnlink(true)}>
              Unlink device
            </button>
          ) : (
            <div className="panel stack">
              <p>Unlink this Pocket from your account? Notes on the device stay on the device.</p>
              <div className="actions-row">
                <button type="button" className="btn btn-danger" disabled={busy} onClick={() => void onUnlink()}>
                  Unlink
                </button>
                <button type="button" className="btn btn-secondary" onClick={() => setConfirmUnlink(false)}>
                  Cancel
                </button>
              </div>
            </div>
          )}
        </>
      )}
    </div>
  )
}
