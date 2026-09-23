import { Link } from 'react-router-dom'

export function WordMark({ to = '/' }: { to?: string }) {
  return (
    <Link to={to} className="wordmark">
      Pocket
    </Link>
  )
}
