import { Link, useNavigate } from 'react-router-dom'
import { useAuth } from '../lib/auth'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function AccountPage() {
  useDocumentTitle('Account')
  const { user, logout } = useAuth()
  const navigate = useNavigate()

  async function onSignOut() {
    await logout()
    navigate('/login', { replace: true })
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
        {user?.is_admin || user?.role === 'admin' ? (
          <Link className="btn btn-secondary" to="/admin/">
            Pocket Cloud Admin
          </Link>
        ) : null}
      </div>
    </div>
  )
}
