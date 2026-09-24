import { type FormEvent, useEffect, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import type { Device } from '../lib/types'
import { relativeTime, sanitizeDeviceName, validateDeviceName } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

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

  useDocumentTitle(device?.device_name || 'Pocket')

  async function load() {
    setError(null)
    try {
      const d = await api.getDevice(id)
      setDevice(d)
      setName(d.device_name || 'Pocket')
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
      setDevice(d)
      setMsg('Name saved.')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
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

  const statusLabel = !device
    ? ''
    : [
        'Linked',
        device.last_seen_at && Date.now() - new Date(device.last_seen_at).getTime() > 1000 * 60 * 60 * 24
          ? 'Offline'
          : null,
        isEntitled ? 'Cloud active' : 'Cloud not active',
      ]
        .filter(Boolean)
        .join(' · ')

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
          ) : null}

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

          <div className="panel stack">
            <h2 style={{ margin: 0, fontSize: '1.05rem' }}>Wi‑Fi networks</h2>
            <p className="muted" style={{ margin: 0 }}>
              Pocket remembers several networks and reconnects after power-off. To add another while it is
              already online:
            </p>
            <ol className="stack-sm" style={{ paddingLeft: '1.2rem', margin: 0 }}>
              <li>
                On Pocket: <strong>Settings → Wi‑Fi → Add with phone…</strong>
              </li>
              <li>Join the Pocket Wi‑Fi shown on the device (it can stay on the first network).</li>
              <li>Open Link below and send the new network’s name and password.</li>
            </ol>
            <Link className="btn btn-primary btn-block" to="/link?add=1">
              Add Wi‑Fi network
            </Link>
          </div>

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
