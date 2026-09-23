import { type FormEvent, useState } from 'react'
import { Link, Navigate, useSearchParams } from 'react-router-dom'
import { api, isApiConfigured, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ApiError } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { WordMark } from '../components/WordMark'
import { useDocumentTitle } from '../components/useDocumentTitle'

type State = 'idle' | 'sending' | 'sent' | 'error'

export function LoginPage() {
  useDocumentTitle('Sign in')
  const { isAuthenticated, loading, offline, error: authError } = useAuth()
  const [params] = useSearchParams()
  const returnTo = params.get('return_to') || '/'
  const [email, setEmail] = useState('')
  const [state, setState] = useState<State>('idle')
  const [error, setError] = useState<string | null>(null)
  const cloudReady = isApiConfigured()

  if (!loading && isAuthenticated) {
    return <Navigate to={returnTo} replace />
  }

  async function onSubmit(e: FormEvent) {
    e.preventDefault()
    if (!cloudReady) return
    setState('sending')
    setError(null)
    try {
      await api.requestMagicLink(email.trim())
      setState('sent')
    } catch (err) {
      setState('error')
      if (isNetworkError(err)) {
        setError(
          err instanceof ApiError && err.code === 'not_connected'
            ? 'Pocket Cloud is not connected yet.'
            : "You're offline or the server is unreachable.",
        )
      } else if (err instanceof ApiError) {
        setError("Couldn't send link. Try again.")
      } else {
        setError("Couldn't send link. Try again.")
      }
    }
  }

  return (
    <div className="page stack" style={{ maxWidth: '24rem', paddingTop: '3rem' }}>
      <WordMark to="/login" />
      <div className="stack-sm">
        <h1>Sign in to Pocket Cloud</h1>
      </div>

      {!cloudReady || offline ? (
        <div className="panel stack-sm" role="status">
          <p>
            <strong>Not connected</strong> — Pocket Cloud API is not hosted yet. You can still open this
            companion shell; sign-in and sync will work once Cloud is deployed.
          </p>
          {authError ? <p className="muted">{authError}</p> : null}
        </div>
      ) : null}

      {state === 'sent' ? (
        <div className="panel">
          <p>Check your email for a sign-in link.</p>
        </div>
      ) : (
        <form className="stack" onSubmit={(e) => void onSubmit(e)}>
          <div className="field">
            <label htmlFor="email">Email</label>
            <input
              id="email"
              name="email"
              type="email"
              autoComplete="email"
              required
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              disabled={state === 'sending' || !cloudReady}
            />
          </div>
          {error ? (
            error.includes('unreachable') || error.includes('not connected') ? (
              <ErrorState message={error} onRetry={() => setError(null)} />
            ) : (
              <p className="muted" role="alert">
                {error}
              </p>
            )
          ) : null}
          <button
            type="submit"
            className="btn btn-primary btn-block"
            disabled={state === 'sending' || !cloudReady}
          >
            {state === 'sending' ? 'Sending…' : 'Email me a link'}
          </button>
        </form>
      )}

      <p className="footer-note">Pocket Cloud syncs Notes from your Pocket device.</p>
      {returnTo.startsWith('/pair') ? (
        <p className="muted" style={{ textAlign: 'center', fontSize: '0.875rem' }}>
          After sign-in you&apos;ll continue linking your Pocket.{' '}
          <Link to={returnTo}>Back to pair</Link>
        </p>
      ) : null}
    </div>
  )
}
