import { useState } from 'react'
import { Link } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { formatDate } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function BillingPage() {
  useDocumentTitle('Billing')
  const { entitlement, refresh } = useAuth()
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  const trialAvailable = entitlement && !entitlement.trial_consumed && (entitlement.status === 'free' || entitlement.status === 'lapsed')
  const isLapsed = entitlement?.status === 'lapsed'
  const isEntitled = entitlement?.entitled

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

  let statusLine: string | null = null
  if (entitlement?.status === 'trialing' && entitlement.trial_ends_at) {
    statusLine = `Trial ends ${formatDate(entitlement.trial_ends_at)}`
  } else if (entitlement?.status === 'active' && entitlement.cancel_at_period_end && entitlement.current_period_end) {
    statusLine = `Canceled · access until ${formatDate(entitlement.current_period_end)}`
  } else if (entitlement?.status === 'active' && entitlement.current_period_end) {
    statusLine = `Next bill ${formatDate(entitlement.current_period_end)}`
  } else if (entitlement?.status === 'past_due') {
    statusLine = 'Payment issue'
  }

  const primaryLabel = trialAvailable ? 'Start free trial' : isLapsed ? 'Resubscribe' : isEntitled ? 'Subscribe' : 'Subscribe'

  return (
    <div className="page stack">
      <h1>Pocket Cloud</h1>

      {error ? <ErrorState message={error} onRetry={() => void refresh()} /> : null}

      <section className="panel stack">
        <p className="hero-price">$3.99/mo</p>
        {trialAvailable ? <p className="muted">7 days free</p> : null}
        {statusLine ? <p className="chip chip-accent">{statusLine}</p> : null}

        <div className="row-between disabled-row" aria-disabled="true">
          <span>Yearly</span>
          <span className="muted">Coming soon</span>
        </div>

        <p className="muted">Cancel anytime. USD.</p>

        <div className="actions">
          {!isEntitled || isLapsed || trialAvailable ? (
            <button type="button" className="btn btn-primary btn-block" disabled={busy} onClick={() => void startCheckout()}>
              {primaryLabel}
            </button>
          ) : null}
          {entitlement?.has_customer || isEntitled ? (
            <button type="button" className="btn btn-secondary btn-block" disabled={busy} onClick={() => void openPortal()}>
              Manage billing
            </button>
          ) : null}
        </div>
      </section>

      <Link to="/account" className="muted">
        Account
      </Link>
    </div>
  )
}

export function BillingSuccessPage() {
  useDocumentTitle("You're all set")
  return (
    <div className="page stack">
      <div className="panel stack">
        <h1>You&apos;re all set</h1>
        <p>Pocket Cloud is active.</p>
        <Link className="btn btn-primary" to="/notes">
          Go to Notes
        </Link>
      </div>
    </div>
  )
}

export function BillingCancelPage() {
  useDocumentTitle('Checkout canceled')
  return (
    <div className="page stack">
      <div className="panel stack">
        <h1>Checkout canceled.</h1>
        <p className="muted">You can subscribe anytime.</p>
        <Link className="btn btn-primary" to="/billing">
          Back to billing
        </Link>
      </div>
    </div>
  )
}
