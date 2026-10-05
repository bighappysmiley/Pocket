import { Link } from 'react-router-dom'

export function WordMark({ to = '/home' }: { to?: string }) {
  return (
    <Link to={to} className="wordmark">
      Pocket Classic
    </Link>
  )
}
