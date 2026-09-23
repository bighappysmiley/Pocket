import { Navigate, Outlet, useLocation } from 'react-router-dom'
import { useAuth } from '../lib/auth'
import { A2HSBanner } from './A2HSBanner'
import { BottomNav, DesktopNav } from './Nav'
import { WordMark } from './WordMark'

export function AppLayout({ bare = false }: { bare?: boolean }) {
  return (
    <div className={bare ? 'app-shell app-shell--bare' : 'app-shell'}>
      <A2HSBanner />
      {!bare ? (
        <header className="page" style={{ paddingBottom: 0 }}>
          <div className="topbar" style={{ marginBottom: 0 }}>
            <WordMark />
            <DesktopNav />
          </div>
        </header>
      ) : null}
      <Outlet />
      {!bare ? <BottomNav /> : null}
    </div>
  )
}

export function RequireAuth() {
  const { isAuthenticated, loading } = useAuth()
  const location = useLocation()

  if (loading) {
    return (
      <div className="page">
        <p className="muted">Loading…</p>
      </div>
    )
  }

  if (!isAuthenticated) {
    const returnTo = `${location.pathname}${location.search}`
    return <Navigate to={`/login?return_to=${encodeURIComponent(returnTo)}`} replace />
  }

  return <Outlet />
}

/** Free / lapsed → /upgrade for Notes, Lists, Backup, Connectors (Spec §7). */
export function RequireEntitlement() {
  const { isEntitled, loading, entitlement } = useAuth()

  if (loading) {
    return (
      <div className="page">
        <p className="muted">Loading…</p>
      </div>
    )
  }

  if (!isEntitled) {
    const pastDue = entitlement?.status === 'past_due'
    return <Navigate to={pastDue ? '/upgrade?reason=past_due' : '/upgrade'} replace />
  }

  return <Outlet />
}
