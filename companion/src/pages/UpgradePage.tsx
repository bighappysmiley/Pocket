import { useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function UpgradePage() {
  useDocumentTitle("Pocket Cloud isn't active")
  const { entitlement } = useAuth()
  const [params] = useSearchParams()
  const pastDue = params.get('reason') === 'past_due' || entitlement?.status === 'past_due'
  const trialAvailable = entitlement && !entitlement.trial_consumed
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  async function startCheckout() {
    setBusy(true)
    setError(null)
    try {
      const { url } = await api.createCheckout()
      window.location.href = url
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
      setBusy(false)
    }
  }

  async function openPortal() {
    setBusy(true)
    setError(null)
    try {
      const { url } = await api.createPortal()
      window.location.href = url
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
      setBusy(false)
    }
  }

  if (pastDue) {
    return (
      <div className="page stack">
        <div className="panel stack">
          <h1>Update your payment to keep syncing.</h1>
          {error ? <ErrorState message={error} onRetry={() => void openPortal()} /> : null}
          <button type="button" className="btn btn-primary" disabled={busy} onClick={() => void openPortal()}>
            Update payment
          </button>
          <Link to="/" className="btn btn-ghost">
            Home
          </Link>
        </div>
      </div>
    )
  }

  return (
    <div className="page stack">
      <div className="panel stack">
        <h1>Pocket Cloud isn&apos;t active</h1>
        <p className="muted">
          Your Notes still live on your Pocket device. Subscribe to sync to your phone, back up, and keep copies
          where you already work.
        </p>
        {error ? <ErrorState message={error} onRetry={() => void startCheckout()} /> : null}
        <div className="actions">
          <button type="button" className="btn btn-primary btn-block" disabled={busy} onClick={() => void startCheckout()}>
            Subscribe — $3.99/mo
          </button>
          {trialAvailable ? (
            <button type="button" className="btn btn-secondary btn-block" disabled={busy} onClick={() => void startCheckout()}>
              Start free trial
            </button>
          ) : null}
          {entitlement?.has_customer ? (
            <button type="button" className="btn btn-ghost btn-block" disabled={busy} onClick={() => void openPortal()}>
              Manage billing
            </button>
          ) : (
            <Link className="btn btn-ghost btn-block" to="/billing">
              Manage billing
            </Link>
          )}
        </div>
      </div>
    </div>
  )
}
