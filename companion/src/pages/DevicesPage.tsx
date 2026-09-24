import { useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import type { Device } from '../lib/types'
import { relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function DevicesPage() {
  useDocumentTitle('Devices')
  const [devices, setDevices] = useState<Device[]>([])
  const [error, setError] = useState<string | null>(null)
  const [showHelp, setShowHelp] = useState(false)
  const [loading, setLoading] = useState(true)

  async function load() {
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
  }

  useEffect(() => {
    void load()
  }, [])

  return (
    <div className="page stack">
      <div className="row-between">
        <h1>Devices</h1>
        <button type="button" className="btn btn-primary" onClick={() => setShowHelp(true)}>
          Link a Pocket
        </button>
      </div>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      {showHelp ? (
        <div className="panel stack-sm" role="dialog" aria-label="Link instructions">
          <p>On your Pocket: open Link, then continue in this app.</p>
          <div className="actions-row">
            <Link className="btn btn-primary" to="/link">
              Link a Pocket
            </Link>
            <button type="button" className="btn btn-ghost" onClick={() => setShowHelp(false)}>
              Close
            </button>
          </div>
        </div>
      ) : null}

      {loading ? (
        <p className="muted">Loading…</p>
      ) : devices.length === 0 ? (
        <div className="empty panel stack">
          <h2>No Pocket linked yet</h2>
          <p className="muted">
            On Pocket Version 1, open Link. Enter Wi‑Fi in this Companion app, then enter the pairing
            code shown on the device.
          </p>
          <Link className="btn btn-primary" to="/link">
            Link a Pocket
          </Link>
        </div>
      ) : (
        <ul className="list">
          {devices.map((d) => (
            <li key={d.id}>
              <Link className="list-link" to={`/devices/${d.id}`}>
                <div className="list-title">{d.device_name || 'Pocket'}</div>
                <div className="list-meta">
                  {d.last_seen_at &&
                  Date.now() - new Date(d.last_seen_at).getTime() < 1000 * 60 * 15
                    ? `Online · last seen ${relativeTime(d.last_seen_at)}`
                    : d.last_seen_at
                      ? `Offline · last seen ${relativeTime(d.last_seen_at)}`
                      : 'Waiting to check in'}
                </div>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  )
}
