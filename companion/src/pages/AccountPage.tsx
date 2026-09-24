import { Link, useNavigate } from 'react-router-dom'
import { type FormEvent, useState } from 'react'
import { clearApiBaseOverride, getApiBase, setApiBase } from '../lib/api'
import { useAuth } from '../lib/auth'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function AccountPage() {
  useDocumentTitle('Account')
  const { user, logout, refresh } = useAuth()
  const navigate = useNavigate()
  const [cloudUrl, setCloudUrl] = useState(() => getApiBase())
  const [saved, setSaved] = useState(false)

  async function onSignOut() {
    await logout()
    navigate('/login', { replace: true })
  }

  function onSaveCloud(e: FormEvent) {
    e.preventDefault()
    const next = cloudUrl.trim()
    if (!next) {
      clearApiBaseOverride()
    } else {
      setApiBase(next)
    }
    setSaved(true)
    void refresh()
  }

  return (
    <div className="page stack">
      <h1>Account</h1>

      <div className="panel stack">
        <div className="field">
          <label>Email</label>
          <p>{user?.email ?? '—'}</p>
        </div>
        <button type="button" className="btn btn-secondary" onClick={() => void onSignOut()}>
          Sign out
        </button>
      </div>

      <form className="panel stack" onSubmit={onSaveCloud}>
        <h2>Pocket Cloud</h2>
        <p className="muted">API origin used for sign-in, pairing, and sync.</p>
        <div className="field">
          <label htmlFor="cloud-url">Cloud API URL</label>
          <input
            id="cloud-url"
            name="cloud-url"
            type="url"
            value={cloudUrl}
            onChange={(e) => {
              setCloudUrl(e.target.value)
              setSaved(false)
            }}
            placeholder="https://pocket-cloud.example.com"
          />
        </div>
        <button type="submit" className="btn btn-secondary">
          Save Cloud URL
        </button>
        {saved ? <p className="muted">Saved.</p> : null}
      </form>

      <div className="actions-row">
        <Link className="btn btn-ghost" to="/devices">
          Devices
        </Link>
        <Link className="btn btn-primary" to="/pair">
          Link a Pocket
        </Link>
        <Link className="btn btn-ghost" to="/billing">
          Billing
        </Link>
        <Link className="btn btn-ghost" to="/connectors">
          Connectors
        </Link>
        <Link className="btn btn-ghost" to="/backup">
          Backup
        </Link>
      </div>
    </div>
  )
}
