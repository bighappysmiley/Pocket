import { type FormEvent, useCallback, useEffect, useState } from 'react'
import { Link, useParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import type { Device } from '../lib/types'
import {
  relativeTime,
  sanitizeDeviceName,
  sanitizeLockMessage,
  validateDeviceName,
} from '../lib/utils'
import {
  getDeviceStatus,
  scanDeviceNetworks,
  sendDeviceWifi,
  type DeviceProvisionStatus,
} from '../lib/deviceWifi'
import { ErrorState } from '../components/ErrorState'
import { WordMark } from '../components/WordMark'
import { useDocumentTitle } from '../components/useDocumentTitle'
import './control.css'

function isOnline(lastSeen: string | null | undefined): boolean {
  if (!lastSeen) return false
  return Date.now() - new Date(lastSeen).getTime() < 1000 * 60 * 15
}

type WifiMode = 'cloud' | 'softap'

export function ControlDevicePage() {
  const { id = '' } = useParams()
  const { isAuthenticated, loading: authLoading, user, logout } = useAuth()
  const [device, setDevice] = useState<Device | null>(null)
  const [name, setName] = useState('')
  const [lockMessage, setLockMessage] = useState('')
  const [volume, setVolume] = useState(80)
  const [brightness, setBrightness] = useState(50)
  const [wifiSsid, setWifiSsid] = useState('')
  const [wifiPassword, setWifiPassword] = useState('')
  const [wifiMode, setWifiMode] = useState<WifiMode>('cloud')
  const [softapStatus, setSoftapStatus] = useState<DeviceProvisionStatus | null>(null)
  const [networks, setNetworks] = useState<string[]>([])
  const [error, setError] = useState<string | null>(null)
  const [msg, setMsg] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const [loadError, setLoadError] = useState<string | null>(null)

  useDocumentTitle(device?.device_name ? `Control · ${device.device_name}` : 'Control Pocket')

  const load = useCallback(async () => {
    setLoadError(null)
    try {
      const d = await api.getDevice(id)
      setDevice(d)
      setName(d.device_name || 'Pocket')
      setLockMessage(d.lock_message || '')
      setVolume(typeof d.volume_percent === 'number' ? d.volume_percent : 80)
      setBrightness(typeof d.brightness_percent === 'number' ? d.brightness_percent : 50)
    } catch (err) {
      if (isNetworkError(err)) setLoadError("You're offline or the server is unreachable.")
      else setLoadError('Something went wrong.')
    }
  }, [id])

  useEffect(() => {
    if (!isAuthenticated) return
    void load()
  }, [isAuthenticated, load])

  const trySoftAp = useCallback(async () => {
    try {
      const st = await getDeviceStatus()
      setSoftapStatus(st)
      const sc = await scanDeviceNetworks()
      setNetworks(sc.networks || [])
      const preferred = st.preferred_ssid || sc.networks?.[0] || ''
      setWifiSsid((prev) => prev || preferred)
      setMsg(`Reached Pocket SoftAP (${st.ap_ssid || 'Pocket-XXXX'}). Pick a network below.`)
      setError(null)
      return true
    } catch {
      setSoftapStatus(null)
      setNetworks([])
      return false
    }
  }, [])

  useEffect(() => {
    if (wifiMode !== 'softap') return
    void trySoftAp()
    const timer = window.setInterval(() => void trySoftAp(), 3000)
    return () => window.clearInterval(timer)
  }, [wifiMode, trySoftAp])

  async function onSaveName(e: FormEvent) {
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
      setMsg('Name saved. Pocket picks it up on its next check-in.')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Could not save name.')
    } finally {
      setBusy(false)
    }
  }

  async function onSaveSettings(e: FormEvent) {
    e.preventDefault()
    setBusy(true)
    setError(null)
    setMsg(null)
    try {
      const d = await api.updateDevice(id, {
        lock_message: lockMessage.trim(),
        volume_percent: volume,
        brightness_percent: brightness,
      })
      setDevice({ ...d, last_seen_at: d.last_seen_at ?? device?.last_seen_at ?? null })
      setLockMessage(d.lock_message || '')
      setVolume(typeof d.volume_percent === 'number' ? d.volume_percent : volume)
      setBrightness(typeof d.brightness_percent === 'number' ? d.brightness_percent : brightness)
      setMsg('Settings saved. Pocket applies them on its next check-in while online.')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Could not save settings.')
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
      if (wifiMode === 'softap') {
        await sendDeviceWifi(wifiSsid.trim(), wifiPassword)
        setMsg('Sent over SoftAP. Pocket is connecting — rejoin your Mac’s normal Wi‑Fi.')
        setWifiPassword('')
      } else {
        if (!online) {
          setError(
            'Pocket looks offline. Cloud Wi‑Fi needs the device online. Switch to SoftAP below, or bring Pocket online first.',
          )
          setBusy(false)
          return
        }
        await api.queueDeviceWifi(id, { ssid: wifiSsid.trim(), password: wifiPassword })
        setMsg(
          `Wi‑Fi “${wifiSsid.trim()}” queued. Pocket applies it within about a minute while online — no SoftAP hop.`,
        )
        setWifiPassword('')
      }
    } catch (err) {
      if (isNetworkError(err)) {
        setError(
          wifiMode === 'softap'
            ? 'Could not reach Pocket at 192.168.4.1. Join the Pocket-XXXX network on this Mac first.'
            : "You're offline or the server is unreachable.",
        )
      } else setError(err instanceof Error ? err.message : 'Could not send Wi‑Fi.')
    } finally {
      setBusy(false)
    }
  }

  const online = device ? isOnline(device.last_seen_at) : false

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
        </header>
        <p className="muted">Sign in to control this Pocket.</p>
        <Link className="btn btn-primary" to={`/login?return_to=${encodeURIComponent(`/control/devices/${id}`)}`}>
          Sign in
        </Link>
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
          <Link className="btn btn-ghost" to="/control">
            All devices
          </Link>
          <button type="button" className="btn btn-ghost" onClick={() => void logout()}>
            Sign out
          </button>
        </div>
      </header>

      {loadError ? <ErrorState message={loadError} onRetry={() => void load()} /> : null}
      {error ? (
        <p role="alert" className="control-alert">
          {error}
        </p>
      ) : null}
      {msg ? <p className="control-notice">{msg}</p> : null}

      {!device ? (
        !loadError ? <p className="muted">Loading…</p> : null
      ) : (
        <>
          <div className="control-hero stack">
            <p className="eyebrow">Mac control</p>
            <div className="row-between" style={{ alignItems: 'flex-start', gap: '1rem' }}>
              <h1 style={{ margin: 0 }}>{device.device_name || 'Pocket'}</h1>
              <span className={`chip ${online ? 'chip-accent' : ''}`}>{online ? 'Online' : 'Offline'}</span>
            </div>
            <p className="muted" style={{ margin: 0 }}>
              {device.last_seen_at
                ? `Last seen ${relativeTime(device.last_seen_at)}`
                : 'Waiting for Pocket to check in over Wi‑Fi.'}
            </p>
            {!online ? (
              <div className="control-offline panel stack-sm">
                <strong>Device offline</strong>
                <p className="muted" style={{ margin: 0 }}>
                  Cloud Wi‑Fi and settings wait until Pocket is online. You can still queue a name, or
                  use SoftAP below if Pocket is broadcasting its setup network.
                </p>
              </div>
            ) : null}
          </div>

          <section className="panel stack control-panel" aria-labelledby="wifi-heading">
            <h2 id="wifi-heading" style={{ margin: 0, fontSize: '1.15rem' }}>
              Connect additional Wi‑Fi
            </h2>
            <p className="muted" style={{ margin: 0 }}>
              Add a home network or hotspot. Prefer Cloud push when Pocket is already online; use SoftAP
              only when you can join the Pocket-XXXX network from this Mac.
            </p>

            <div className="control-mode-toggle" role="tablist" aria-label="Wi‑Fi method">
              <button
                type="button"
                role="tab"
                aria-selected={wifiMode === 'cloud'}
                className={wifiMode === 'cloud' ? 'is-active' : undefined}
                onClick={() => {
                  setWifiMode('cloud')
                  setError(null)
                }}
              >
                Cloud (online)
              </button>
              <button
                type="button"
                role="tab"
                aria-selected={wifiMode === 'softap'}
                className={wifiMode === 'softap' ? 'is-active' : undefined}
                onClick={() => {
                  setWifiMode('softap')
                  setError(null)
                  setMsg(null)
                }}
              >
                SoftAP (local)
              </button>
            </div>

            {wifiMode === 'cloud' && !online ? (
              <p className="muted" style={{ margin: 0 }}>
                Pocket must be online for Cloud Wi‑Fi. Bring it onto a known network first, or switch to
                SoftAP.
              </p>
            ) : null}

            {wifiMode === 'softap' ? (
              <div className="stack-sm">
                <ol className="control-steps">
                  <li>
                    On this Mac, join <strong>{softapStatus?.ap_ssid || 'Pocket-XXXX'}</strong> (password
                    shown on Pocket).
                  </li>
                  <li>Return here — we poll 192.168.4.1 automatically.</li>
                </ol>
                <button type="button" className="btn btn-secondary" onClick={() => void trySoftAp()}>
                  {softapStatus ? 'Refresh SoftAP scan' : 'I’ve joined Pocket Wi‑Fi — check'}
                </button>
                {softapStatus ? (
                  <p className="muted" style={{ margin: 0, fontSize: '0.9rem' }}>
                    SoftAP reachable · {networks.length} network{networks.length === 1 ? '' : 's'} found
                  </p>
                ) : (
                  <p className="muted" style={{ margin: 0, fontSize: '0.9rem' }}>
                    Not reaching Pocket yet at 192.168.4.1.
                  </p>
                )}
              </div>
            ) : null}

            <form className="stack" onSubmit={(e) => void onQueueWifi(e)}>
              <div className="field">
                <label htmlFor="control_wifi_ssid">Network name</label>
                {wifiMode === 'softap' && networks.length > 0 ? (
                  <select
                    id="control_wifi_ssid"
                    value={wifiSsid}
                    onChange={(e) => setWifiSsid(e.target.value)}
                    required
                  >
                    {networks.map((n) => (
                      <option key={n} value={n}>
                        {n}
                      </option>
                    ))}
                  </select>
                ) : (
                  <input
                    id="control_wifi_ssid"
                    value={wifiSsid}
                    onChange={(e) => setWifiSsid(e.target.value)}
                    placeholder="Home or hotspot name"
                    autoComplete="off"
                    required
                  />
                )}
              </div>
              <div className="field">
                <label htmlFor="control_wifi_password">Password</label>
                <input
                  id="control_wifi_password"
                  type="password"
                  value={wifiPassword}
                  onChange={(e) => setWifiPassword(e.target.value)}
                  autoComplete="current-password"
                />
              </div>
              <button
                type="submit"
                className="btn btn-primary"
                disabled={busy || (wifiMode === 'cloud' && !online)}
              >
                {busy
                  ? 'Sending…'
                  : wifiMode === 'softap'
                    ? 'Send over SoftAP'
                    : 'Queue Wi‑Fi on Pocket'}
              </button>
            </form>
          </section>

          <form className="panel stack control-panel" onSubmit={(e) => void onSaveName(e)}>
            <h2 style={{ margin: 0, fontSize: '1.15rem' }}>Device name</h2>
            <div className="field">
              <label htmlFor="control_device_name">Name</label>
              <input
                id="control_device_name"
                value={name}
                maxLength={20}
                onChange={(e) => setName(sanitizeDeviceName(e.target.value))}
              />
              <span className="muted" style={{ fontSize: '0.8rem' }}>
                {name.length}/20
              </span>
            </div>
            <button type="submit" className="btn btn-primary" disabled={busy} style={{ alignSelf: 'flex-start' }}>
              Save name
            </button>
          </form>

          <form className="panel stack control-panel" onSubmit={(e) => void onSaveSettings(e)}>
            <h2 style={{ margin: 0, fontSize: '1.15rem' }}>Settings</h2>
            <p className="muted" style={{ margin: 0 }}>
              Same values as on-device Settings. Applied when Pocket checks in over Wi‑Fi.
            </p>
            <div className="field">
              <label htmlFor="control_lock_message">Lock screen message (optional)</label>
              <input
                id="control_lock_message"
                value={lockMessage}
                maxLength={40}
                placeholder="e.g. Emma's Pocket"
                onChange={(e) => setLockMessage(sanitizeLockMessage(e.target.value))}
              />
            </div>
            <div className="field">
              <label htmlFor="control_volume">Speaker volume — {volume}%</label>
              <input
                id="control_volume"
                type="range"
                min={0}
                max={100}
                step={10}
                value={volume}
                onChange={(e) => setVolume(Number(e.target.value))}
              />
            </div>
            <div className="field">
              <label htmlFor="control_brightness">Display brightness — {brightness}%</label>
              <input
                id="control_brightness"
                type="range"
                min={0}
                max={100}
                step={10}
                value={brightness}
                onChange={(e) => setBrightness(Number(e.target.value))}
              />
            </div>
            <button type="submit" className="btn btn-primary" disabled={busy} style={{ alignSelf: 'flex-start' }}>
              Save settings
            </button>
          </form>
        </>
      )}
    </div>
  )
}
