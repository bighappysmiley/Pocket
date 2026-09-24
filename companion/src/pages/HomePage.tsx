import { useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import type { Device, Note } from '../lib/types'
import { ApiError } from '../lib/types'
import { daysLeft, relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function HomePage() {
  useDocumentTitle('Home')
  const { entitlement, isEntitled, refresh, offline } = useAuth()
  const [notes, setNotes] = useState<Note[]>([])
  const [devices, setDevices] = useState<Device[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      setLoading(true)
      setError(null)
      try {
        const devicesRes = await api.listDevices()
        if (cancelled) return
        setDevices(devicesRes.devices)
        if (isEntitled) {
          try {
            const notesRes = await api.listNotes()
            if (cancelled) return
            setNotes(notesRes.notes.filter((n) => !n.deleted_at).slice(0, 3))
          } catch (notesErr) {
            if (cancelled) return
            // Devices still useful; surface notes failure without blanking the home shell.
            if (isNetworkError(notesErr) || offline) {
              setError("You're offline or the server is unreachable.")
            } else if (notesErr instanceof ApiError) {
              setError(notesErr.message || 'Could not load notes.')
            } else {
              setError('Could not load notes.')
            }
            setNotes([])
          }
        } else {
          setNotes([])
        }
      } catch (err) {
        if (cancelled) return
        if (isNetworkError(err) || offline) {
          setError("You're offline or the server is unreachable.")
        } else if (err instanceof ApiError) {
          setError(err.message || 'Something went wrong.')
        } else {
          setError('Something went wrong.')
        }
      } finally {
        if (!cancelled) setLoading(false)
      }
    })()
    return () => {
      cancelled = true
    }
  }, [isEntitled, offline])

  const trialDays = daysLeft(entitlement?.trial_ends_at)
  const trialAvailable = entitlement && !entitlement.trial_consumed && entitlement.status === 'free'

  return (
    <div className="page stack">
      <div className="stack-sm">
        <h1 className="sr-only">Home</h1>
        {isEntitled ? (
          <p className="muted">Synced with your Pocket</p>
        ) : (
          <p className="muted">Welcome</p>
        )}
      </div>

      {error ? <ErrorState message={error} onRetry={() => void refresh()} /> : null}

      {isEntitled ? (
        <>
          {entitlement?.status === 'trialing' && trialDays !== null ? (
            <div className="panel row-between">
              <span className="chip chip-accent">Trial · {trialDays} days left</span>
              <Link to="/billing" className="btn btn-ghost">
                Manage
              </Link>
            </div>
          ) : null}

          <section className="stack-sm">
            <div className="row-between">
              <h2>Notes</h2>
              <Link to="/notes">View all</Link>
            </div>
            {loading ? (
              <p className="muted">Loading…</p>
            ) : notes.length === 0 ? (
              <p className="muted">No notes yet</p>
            ) : (
              <ul className="list">
                {notes.map((n) => (
                  <li key={n.id}>
                    <Link className="list-link" to={`/notes/${n.id}`}>
                      <div className="list-title">{n.title || 'Untitled'}</div>
                      <div className="list-meta">{relativeTime(n.updated_at)}</div>
                    </Link>
                  </li>
                ))}
              </ul>
            )}
          </section>

          <section className="actions-row">
            <Link className="btn btn-secondary" to="/lists">
              Lists
            </Link>
            <Link className="btn btn-secondary" to="/devices">
              Devices
            </Link>
            <Link className="btn btn-secondary" to="/billing">
              Billing
            </Link>
          </section>
        </>
      ) : (
        <section className="panel stack">
          <div className="stack-sm">
            <h2>Pocket Cloud</h2>
            <p className="hero-price">$3.99/mo</p>
            {trialAvailable ? <p className="muted">7 days free</p> : null}
          </div>
          <p className="muted">
            Sync Notes &amp; Lists to your phone, back up, and keep copies where you already work.
          </p>
          <div className="actions">
            {trialAvailable ? (
              <Link className="btn btn-primary" to="/billing">
                Start free trial
              </Link>
            ) : entitlement?.status === 'lapsed' ? (
              <Link className="btn btn-primary" to="/billing">
                Resubscribe
              </Link>
            ) : (
              <Link className="btn btn-primary" to="/upgrade">
                Subscribe — $3.99/mo
              </Link>
            )}
          </div>
        </section>
      )}

      <section className="panel stack-sm">
        <h2>Devices</h2>
        {devices.length === 0 ? (
          <>
            <p className="muted">No Pocket linked yet</p>
            <Link className="btn btn-primary" to="/link">
              Link a Pocket
            </Link>
          </>
        ) : (
          <ul className="list">
            {devices.map((d) => (
              <li key={d.id}>
                <Link className="list-link" to={`/devices/${d.id}`}>
                  <div className="list-title">{d.device_name || 'Pocket'}</div>
                  <div className="list-meta">
                    {d.last_seen_at ? `Last seen ${relativeTime(d.last_seen_at)}` : 'Waiting to link'}
                  </div>
                </Link>
              </li>
            ))}
          </ul>
        )}
      </section>
    </div>
  )
}
