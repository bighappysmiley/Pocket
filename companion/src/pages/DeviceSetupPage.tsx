import { type FormEvent, useEffect, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { sanitizeDeviceName, validateDeviceName } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function DeviceSetupPage() {
  useDocumentTitle('Name your Pocket')
  const { id = '' } = useParams()
  const navigate = useNavigate()
  const [name, setName] = useState('Pocket')
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        const d = await api.getDevice(id)
        if (!cancelled) setName(d.device_name || 'Pocket')
      } catch {
        // Keep default
      }
    })()
    return () => {
      cancelled = true
    }
  }, [id])

  async function save(value: string) {
    const trimmed = value.trim() || 'Pocket'
    const validation = validateDeviceName(trimmed)
    if (validation && trimmed !== 'Pocket') {
      setError(validation)
      return
    }
    setBusy(true)
    setError(null)
    try {
      await api.updateDevice(id, { device_name: trimmed.slice(0, 20) })
      navigate(`/devices/${id}`, { replace: true })
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setBusy(false)
    }
  }

  function onSubmit(e: FormEvent) {
    e.preventDefault()
    const trimmed = name.trim()
    if (!trimmed) {
      setError('Enter a name')
      return
    }
    void save(trimmed)
  }

  return (
    <div className="page stack">
      <div className="stack-sm">
        <h1>Name your Pocket</h1>
        <p className="muted">This name appears on your phone. You can change it later.</p>
      </div>

      <form className="panel stack" onSubmit={onSubmit}>
        <div className="field">
          <label htmlFor="device_name">Device name</label>
          <input
            id="device_name"
            name="device_name"
            value={name}
            maxLength={20}
            placeholder="Pocket"
            onChange={(e) => setName(sanitizeDeviceName(e.target.value))}
          />
          <span className="muted" style={{ fontSize: '0.8rem' }}>
            {name.length}/20
          </span>
        </div>

        {error ? (
          error.includes('unreachable') ? (
            <ErrorState message={error} onRetry={() => void save(name)} />
          ) : (
            <p role="alert" className="muted">
              {error}
            </p>
          )
        ) : null}

        <button type="submit" className="btn btn-primary btn-block" disabled={busy}>
          Save
        </button>
        <button
          type="button"
          className="btn btn-secondary btn-block"
          disabled={busy}
          onClick={() => void save('Pocket')}
        >
          Use default name
        </button>
        <Link className="btn btn-ghost btn-block" to="/billing">
          Continue to Pocket Cloud
        </Link>
      </form>
    </div>
  )
}
