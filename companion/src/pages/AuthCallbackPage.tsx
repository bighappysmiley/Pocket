import { useEffect, useState } from 'react'
import { Navigate, useNavigate, useSearchParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function AuthCallbackPage() {
  useDocumentTitle('Signing in')
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const { refresh } = useAuth()
  const [error, setError] = useState<string | null>(null)
  const token = params.get('token')
  const returnTo = params.get('return_to') || '/'

  useEffect(() => {
    if (!token) {
      setError('Sign in again to continue.')
      return
    }
    let cancelled = false
    ;(async () => {
      try {
        await api.consumeMagicLink(token)
        await refresh()
        if (!cancelled) navigate(returnTo, { replace: true })
      } catch (err) {
        if (cancelled) return
        if (isNetworkError(err)) {
          setError("You're offline or the server is unreachable.")
        } else {
          setError('Sign in again to continue.')
        }
      }
    })()
    return () => {
      cancelled = true
    }
  }, [token, refresh, navigate, returnTo])

  if (!token && !error) {
    return <Navigate to="/login" replace />
  }

  return (
    <div className="page">
      {error ? (
        <ErrorState
          message={error}
          onRetry={() => navigate(`/login?return_to=${encodeURIComponent(returnTo)}`)}
        />
      ) : (
        <p className="muted">Signing you in…</p>
      )}
    </div>
  )
}
