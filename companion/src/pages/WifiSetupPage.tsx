import { type FormEvent, useCallback, useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import {
  getDeviceStatus,
  scanDeviceNetworks,
  sendDeviceWifi,
  type DeviceProvisionStatus,
} from '../lib/deviceWifi'
import { useDocumentTitle } from '../components/useDocumentTitle'
import { WordMark } from '../components/WordMark'

type Phase = 'instructions' | 'connected' | 'sent' | 'error'

export function WifiSetupPage() {
  useDocumentTitle('Wi-Fi setup')
  const [phase, setPhase] = useState<Phase>('instructions')
  const [status, setStatus] = useState<DeviceProvisionStatus | null>(null)
  const [networks, setNetworks] = useState<string[]>([])
  const [ssid, setSsid] = useState('')
  const [password, setPassword] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const tryReachDevice = useCallback(async () => {
    try {
      const st = await getDeviceStatus()
      setStatus(st)
      const sc = await scanDeviceNetworks()
      setNetworks(sc.networks || [])
      const preferred = st.preferred_ssid || sc.networks?.[0] || ''
      setSsid((prev) => prev || preferred)
      setPhase('connected')
      setError(null)
      return true
    } catch {
      setPhase('instructions')
      return false
    }
  }, [])

  useEffect(() => {
    let cancelled = false
    const tick = async () => {
      if (cancelled) return
      await tryReachDevice()
    }
    void tick()
    const id = window.setInterval(() => {
      if (phase === 'sent') return
      void tick()
    }, 2500)
    return () => {
      cancelled = true
      window.clearInterval(id)
    }
  }, [tryReachDevice, phase])

  async function onSubmit(e: FormEvent) {
    e.preventDefault()
    if (!ssid.trim()) {
      setError('Choose a network.')
      return
    }
    setBusy(true)
    setError(null)
    try {
      await sendDeviceWifi(ssid.trim(), password)
      setPhase('sent')
    } catch (err) {
      setPhase('error')
      setError(err instanceof Error ? err.message : 'Could not send Wi‑Fi to Pocket.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="page stack" style={{ maxWidth: '28rem', paddingTop: '2rem' }}>
      <WordMark to="/" />
      <div className="stack-sm">
        <h1>Wi‑Fi for your Pocket</h1>
        <p className="muted">
          Pocket has a dial, not a keyboard. You type the Wi‑Fi password here on your phone.
        </p>
      </div>

      {phase === 'instructions' || phase === 'error' ? (
        <div className="panel stack">
          <ol className="stack-sm" style={{ paddingLeft: '1.2rem', margin: 0 }}>
            <li>On Pocket, choose a Wi‑Fi network (or “Use phone”).</li>
            <li>
              On this phone, open Wi‑Fi settings and join the open network named like{' '}
              <strong>{status?.ap_ssid || 'Pocket-XXXX'}</strong> shown on the device.
            </li>
            <li>Come back here — this page will continue automatically.</li>
          </ol>
          <p className="muted" style={{ fontSize: '0.9rem' }}>
            Tip: if your phone shows a “Sign in to network” banner after joining, open it — that also
            works.
          </p>
          {error ? (
            <p role="alert" className="muted">
              {error}
            </p>
          ) : null}
          <button type="button" className="btn btn-primary btn-block" onClick={() => void tryReachDevice()}>
            I’ve joined Pocket Wi‑Fi — continue
          </button>
        </div>
      ) : null}

      {phase === 'connected' ? (
        <form className="panel stack" onSubmit={onSubmit}>
          <p className="muted">Connected to Pocket. Choose your home network and enter its password.</p>
          <div className="field">
            <label htmlFor="ssid">Network</label>
            {networks.length > 0 ? (
              <select id="ssid" value={ssid} onChange={(e) => setSsid(e.target.value)} required>
                {networks.map((n) => (
                  <option key={n} value={n}>
                    {n}
                  </option>
                ))}
              </select>
            ) : (
              <input
                id="ssid"
                value={ssid}
                onChange={(e) => setSsid(e.target.value)}
                placeholder="Network name"
                required
              />
            )}
          </div>
          <div className="field">
            <label htmlFor="password">Password</label>
            <input
              id="password"
              type="password"
              autoComplete="current-password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
            />
          </div>
          {error ? (
            <p role="alert" className="muted">
              {error}
            </p>
          ) : null}
          <button type="submit" className="btn btn-primary btn-block" disabled={busy}>
            {busy ? 'Sending…' : 'Connect Pocket'}
          </button>
        </form>
      ) : null}

      {phase === 'sent' ? (
        <div className="panel stack">
          <p>
            Sent. Pocket is connecting. Rejoin your normal home Wi‑Fi on this phone, then create your
            Pocket Cloud account and link the device.
          </p>
          <Link className="btn btn-primary btn-block" to="/login?return_to=/pair">
            Create account / sign in
          </Link>
          <Link className="btn btn-secondary btn-block" to="/pair">
            Link Pocket
          </Link>
        </div>
      ) : null}
    </div>
  )
}
