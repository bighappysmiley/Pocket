import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react'
import type { ReactNode } from 'react'
import { api, getApiBase, isApiConfigured, isNetworkError } from './api'
import { clearMutationQueue, flushMutationQueue, type QueuedMutation } from './queue'
import { ApiError, type Entitlement, type User } from './types'

interface AuthState {
  user: User | null
  entitlement: Entitlement | null
  loading: boolean
  offline: boolean
  error: string | null
  refresh: () => Promise<void>
  logout: () => Promise<void>
  isEntitled: boolean
  isAuthenticated: boolean
}

const AuthContext = createContext<AuthState | null>(null)

const defaultFreeEntitlement = (trialConsumed = false): Entitlement => ({
  status: 'free',
  entitled: false,
  trial_consumed: trialConsumed,
  has_customer: false,
})

async function flushQueued(sendOne: (m: QueuedMutation) => Promise<void>) {
  try {
    await flushMutationQueue(sendOne)
  } catch {
    // Non-fatal — queue retries on next online/refresh
  }
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<User | null>(null)
  const [entitlement, setEntitlement] = useState<Entitlement | null>(null)
  const [loading, setLoading] = useState(true)
  const [offline, setOffline] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const refresh = useCallback(async () => {
    try {
      if (!isApiConfigured() && import.meta.env.PROD) {
        setUser(null)
        setEntitlement(null)
        setOffline(true)
        setError('Pocket Cloud is temporarily unavailable.')
        return
      }

      const me = await api.me()
      setUser(me.user)
      setEntitlement(me.entitlement)
      setOffline(false)
      setError(null)

      if (me.entitlement.entitled) {
        await flushQueued(async (m) => {
          const base = getApiBase()
          const res = await fetch(`${base}${m.path}`, {
            method: m.method,
            credentials: 'include',
            headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
            body: m.body !== undefined ? JSON.stringify(m.body) : undefined,
          })
          if (!res.ok) {
            if (res.status === 403) {
              throw new ApiError('Sync paused. Resubscribe to use Pocket Cloud on this phone.', 403)
            }
            throw new ApiError('flush failed', res.status)
          }
        })
      }
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        setUser(null)
        setEntitlement(null)
        setOffline(false)
        setError(null)
      } else if (isNetworkError(err)) {
        setOffline(true)
        setError(
          err instanceof ApiError && err.code === 'not_connected'
            ? 'Pocket Cloud is temporarily unavailable.'
            : "You're offline or the server is unreachable.",
        )
      } else {
        setError(err instanceof Error ? err.message : 'Something went wrong.')
      }
    } finally {
      setLoading(false)
    }
  }, [])

  const logout = useCallback(async () => {
    try {
      await api.logout()
    } catch {
      // Still clear local state
    }
    await clearMutationQueue()
    setUser(null)
    setEntitlement(null)
  }, [])

  useEffect(() => {
    void refresh()
  }, [refresh])

  useEffect(() => {
    const onOnline = () => {
      void refresh()
    }
    window.addEventListener('online', onOnline)
    return () => window.removeEventListener('online', onOnline)
  }, [refresh])

  const value = useMemo<AuthState>(
    () => ({
      user,
      entitlement: entitlement ?? (user ? defaultFreeEntitlement(user.trial_consumed) : null),
      loading,
      offline,
      error,
      refresh,
      logout,
      isEntitled: Boolean(entitlement?.entitled),
      isAuthenticated: Boolean(user),
    }),
    [user, entitlement, loading, offline, error, refresh, logout],
  )

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext)
  if (!ctx) throw new Error('useAuth must be used within AuthProvider')
  return ctx
}
