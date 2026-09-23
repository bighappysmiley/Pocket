import { type FormEvent, useState } from 'react'
import { Link, Navigate, useNavigate, useSearchParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ApiError } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function PairPage() {
  useDocumentTitle('Link your Pocket')
  const { isAuthenticated, loading: authLoading } = useAuth()
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const codeFromQuery = (params.get('code') || '').toUpperCase()
  const [code, setCode] = useState(codeFromQuery)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  // Preserve code across login via return_to
  if (!authLoading && !isAuthenticated) {
    const returnTo = `/pair${code ? `?code=${encodeURIComponent(code)}` : ''}`
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
        // Continue to claim if metadata endpoint unavailable
        if (!isNetworkError(err) && !(err instanceof ApiError && err.status === 404)) {
          // fall through
        } else if (isNetworkError(err)) {
          setError("You're offline or the server is unreachable.")
          setBusy(false)
          return
        }
      }

      const result = await api.claimPair(trimmed)
      navigate(`/devices/${result.device_id}/setup`, { replace: true })
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
      <div className="page">
        <p className="muted">Loading…</p>
      </div>
    )
  }

  return (
    <div className="page stack">
      <div className="stack-sm">
        <h1>Link your Pocket</h1>
        <p className="muted">You&apos;re connecting a Pocket device to this account.</p>
        {code ? <p className="chip">Code · {code}</p> : null}
      </div>

      <form className="panel stack" onSubmit={(e) => void onClaim(e)}>
        {!codeFromQuery ? (
          <div className="field">
            <label htmlFor="code">Pairing code</label>
            <input
              id="code"
              name="code"
              value={code}
              onChange={(e) => setCode(e.target.value.toUpperCase())}
              maxLength={8}
              autoCapitalize="characters"
              autoCorrect="off"
              spellCheck={false}
              placeholder="XXXXXXXX"
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
        <button type="submit" className="btn btn-primary btn-block" disabled={busy}>
          {busy ? 'Linking…' : 'Link this Pocket'}
        </button>
        <Link className="btn btn-secondary btn-block" to="/devices">
          Cancel
        </Link>
      </form>
    </div>
  )
}
