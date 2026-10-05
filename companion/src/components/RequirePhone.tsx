import { Navigate, Outlet, useLocation } from 'react-router-dom'
import { isPhoneCompanionClient } from '../lib/utils'

/**
 * Blocks desktop browsers from Companion app routes.
 * Marketing `/`, `/developer`, and owner `/admin` stay outside this guard.
 */
export function RequirePhone() {
  const location = useLocation()
  if (isPhoneCompanionClient()) {
    return <Outlet />
  }
  const returnTo = `${location.pathname}${location.search}`
  return (
    <Navigate
      to={`/get-companion?from=${encodeURIComponent(returnTo)}`}
      replace
    />
  )
}
