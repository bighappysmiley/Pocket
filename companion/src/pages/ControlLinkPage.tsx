import { type FormEvent, useState } from 'react'
import { Link, Navigate, useNavigate, useSearchParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ApiError } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { WordMark } from '../components/WordMark'
import { useDocumentTitle } from '../components/useDocumentTitle'
import './control.css'

/** Desktop pairing — same Cloud claim path as phone Companion `/pair`. */
export function ControlLinkPage() {
  useDocumentTitle('Link Pocket')
  const { isAuthenticated, loading: authLoading, user, logout } = useAuth()
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const codeFromQuery = (params.get('code') || '').toUpperCase()
  const [code, setCode] = useState(codeFromQuery)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  if (!authLoading && !isAuthenticated) {
    const returnTo = `/control/link${code ? `?code=${encodeURIComponent(code)}` : ''}`
    return <Navigate to={`/login?return_to=${encodeURIComponent(returnTo)}`} replace />
  }

  async function onClaim(e?: FormEvent) {
    e?.preventDefault()
    const trimmed = code.trim().toUpperCase()
    if (!trimmed) {
      setError("We couldn't find that code.")
      return
    }
    setBusy(true)
    setError(null)
    try {
      try {
        const session = await api.getPairSession(trimmed)
        if (session.status === 'expired') {
          setError('This code has expired. Generate a new one on your Pocket.')
          setBusy(false)
          return
        }
        if (session.status === 'claimed') {
          setError('This Pocket is linked to another account. Unlink it there first.')
          setBusy(false)
          return
        }
      } catch (err) {
        if (err instanceof ApiError && err.status === 404) {
          setError("We couldn't find that code.")
          setBusy(false)
          return
        }
        if (isNetworkError(err)) {
          setError("You're offline or the server is unreachable.")
          setBusy(false)
          return
        }
      }

      const result = await api.claimPair(trimmed)
      navigate(`/control/devices/${result.device_id}`, { replace: true })
    } catch (err) {
      if (isNetworkError(err)) {
        setError("You're offline or the server is unreachable.")
      } else if (err instanceof ApiError) {
        if (err.message.includes('expired') || err.code === 'pair_expired') {
          setError('This code has expired. Generate a new one on your Pocket.')
        } else if (err.code === 'pair_linked' || err.message.toLowerCase().includes('another account')) {
          setError('This Pocket is linked to another account. Unlink it there first.')
        } else if (err.status === 404) {
          setError("We couldn't find that code.")
        } else {
          setError(err.message || "We couldn't find that code.")
        }
      } else {
        setError('Something went wrong.')
      }
    } finally {
      setBusy(false)
    }
  }

  if (authLoading || !isAuthenticated) {
    return (
      <div className="control-shell">
        <p className="muted">Loading…</p>
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

      <div className="control-hero stack">
        <p className="eyebrow">Mac control</p>
        <h1>Link your Pocket</h1>
        <p className="muted control-lede">
          Same Pocket Cloud pairing as Companion. On the device, open Link and enter the code shown
          there. Wi‑Fi for a brand-new device is easiest from Companion SoftAP on your phone — after
          that, add more networks here.
        </p>
        {code ? <p className="chip">Code · {code}</p> : null}
      </div>

      <form className="panel stack control-panel" onSubmit={(e) => void onClaim(e)}>
        {!codeFromQuery ? (
          <div className="field">
            <label htmlFor="control_pair_code">Pairing code</label>
            <input
              id="control_pair_code"
              name="code"
              value={code}
              onChange={(e) => setCode(e.target.value.toUpperCase())}
              maxLength={8}
              autoCapitalize="characters"
              autoCorrect="off"
              spellCheck={false}
              placeholder="XXXXXXXX"
              autoFocus
            />
          </div>
        ) : null}

        {error ? (
          error.includes('unreachable') ? (
            <ErrorState message={error} onRetry={() => void onClaim()} />
          ) : (
            <p role="alert" className="muted">
              {error}
            </p>
          )
        ) : null}

        <button type="submit" className="btn btn-primary" disabled={busy} style={{ alignSelf: 'flex-start' }}>
          {busy ? 'Linking…' : 'Link this Pocket'}
        </button>
        <Link className="btn btn-secondary" to="/control" style={{ alignSelf: 'flex-start' }}>
          Cancel
        </Link>
      </form>
    </div>
  )
}
