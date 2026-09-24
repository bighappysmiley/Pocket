import { type FormEvent, useState } from 'react'
import { Link, Navigate, useSearchParams } from 'react-router-dom'
import { api, isApiConfigured, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ApiError } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { WordMark } from '../components/WordMark'
import { useDocumentTitle } from '../components/useDocumentTitle'

type Mode = 'signin' | 'signup'
type State = 'idle' | 'busy' | 'registered' | 'error'

export function LoginPage() {
  useDocumentTitle('Sign in')
  const { isAuthenticated, loading, offline, error: authError, refresh } = useAuth()
  const [params] = useSearchParams()
  const returnTo = params.get('return_to') || '/'
  const justVerified = params.get('verified') === '1'
  const [mode, setMode] = useState<Mode>('signin')
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [state, setState] = useState<State>('idle')
  const [error, setError] = useState<string | null>(null)

  const cloudReady = isApiConfigured()

  if (!loading && isAuthenticated) {
    return <Navigate to={returnTo} replace />
  }

  async function onSubmit(e: FormEvent) {
    e.preventDefault()
    if (!cloudReady) {
      setError('Pocket Cloud is temporarily unavailable. Try again later.')
      return
    }
    setState('busy')
    setError(null)
    try {
      if (mode === 'signup') {
        await api.register(email.trim(), password)
        setState('registered')
        return
      }
      await api.login(email.trim(), password)
      await refresh()
      setState('idle')
    } catch (err) {
      setState('error')
      if (isNetworkError(err)) {
        setError(
          err instanceof ApiError && err.code === 'not_connected'
            ? 'Pocket Cloud is temporarily unavailable. Try again later.'
            : "You're offline or the server is unreachable.",
        )
      } else if (err instanceof ApiError) {
        setError(err.message || (mode === 'signup' ? "Couldn't create account." : "Couldn't sign in."))
      } else {
        setError(mode === 'signup' ? "Couldn't create account." : "Couldn't sign in.")
      }
    }
  }

  return (
    <div className="page stack" style={{ maxWidth: '24rem', paddingTop: '3rem' }}>
      <WordMark to="/login" />
      <div className="stack-sm">
        <h1>{mode === 'signup' ? 'Create your account' : 'Sign in to Pocket'}</h1>
        <p className="muted">
          {mode === 'signup'
            ? 'We’ll email you a verification link, then you can sign in with your password.'
            : 'Use the email and password for your Pocket Cloud account.'}
        </p>
      </div>

      {justVerified ? (
        <div className="panel" role="status">
          <p>Email verified. Sign in with your password.</p>
        </div>
      ) : null}

      {!cloudReady || offline ? (
        <div className="panel" role="status">
          <p className="muted">
            Pocket Cloud is temporarily unavailable. You can still open this page; try signing in again
            shortly.
          </p>
          {authError ? <p className="muted">{authError}</p> : null}
        </div>
      ) : null}

      <div className="actions-row" style={{ gap: '0.5rem' }}>
        <button
          type="button"
          className={mode === 'signin' ? 'btn btn-primary' : 'btn btn-ghost'}
          onClick={() => {
            setMode('signin')
            setState('idle')
            setError(null)
          }}
        >
          Sign in
        </button>
        <button
          type="button"
          className={mode === 'signup' ? 'btn btn-primary' : 'btn btn-ghost'}
          onClick={() => {
            setMode('signup')
            setState('idle')
            setError(null)
          }}
        >
          Create account
        </button>
      </div>

      {state === 'registered' ? (
        <div className="panel stack-sm">
          <p>Check your email for a verification link.</p>
          <p className="muted">After you verify, come back here and sign in with your password.</p>
          <button
            type="button"
            className="btn btn-secondary"
            onClick={() => {
              setMode('signin')
              setState('idle')
            }}
          >
            Go to sign in
          </button>
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
              disabled={state === 'busy' || !cloudReady}
            />
          </div>
          <div className="field">
            <label htmlFor="password">Password</label>
            <input
              id="password"
              name="password"
              type="password"
              autoComplete={mode === 'signup' ? 'new-password' : 'current-password'}
              required
              minLength={8}
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              disabled={state === 'busy' || !cloudReady}
            />
            {mode === 'signup' ? <p className="muted">At least 8 characters.</p> : null}
          </div>
          {error ? (
            error.includes('unreachable') || error.includes('unavailable') ? (
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
            disabled={state === 'busy' || !cloudReady}
          >
            {state === 'busy'
              ? mode === 'signup'
                ? 'Creating…'
                : 'Signing in…'
              : mode === 'signup'
                ? 'Create account'
                : 'Sign in'}
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
