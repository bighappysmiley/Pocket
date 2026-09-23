import { NavLink } from 'react-router-dom'

const links = [
  { to: '/', label: 'Home', end: true },
  { to: '/notes', label: 'Notes' },
  { to: '/lists', label: 'Lists' },
  { to: '/passes', label: 'Passes' },
  { to: '/devices', label: 'Devices' },
]

export function BottomNav() {
  return (
    <nav className="bottom-nav" aria-label="Primary">
      {links.map((l) => (
        <NavLink key={l.to} to={l.to} end={l.end} className={({ isActive }) => (isActive ? 'active' : undefined)}>
          <span className="nav-label">{l.label}</span>
        </NavLink>
      ))}
    </nav>
  )
}

export function DesktopNav() {
  return (
    <nav className="desktop-nav" aria-label="Primary">
      {links.map((l) => (
        <NavLink key={l.to} to={l.to} end={l.end} className={({ isActive }) => (isActive ? 'active' : undefined)}>
          {l.label}
        </NavLink>
      ))}
      <NavLink to="/account" className={({ isActive }) => (isActive ? 'active' : undefined)}>
        Account
      </NavLink>
    </nav>
  )
}
