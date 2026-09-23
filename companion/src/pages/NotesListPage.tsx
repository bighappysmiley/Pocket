import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { api, isNetworkError, nowIso } from '../lib/api'
import { enqueueMutation } from '../lib/queue'
import type { Note } from '../lib/types'
import { relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function NotesListPage() {
  useDocumentTitle('Notes')
  const navigate = useNavigate()
  const [notes, setNotes] = useState<Note[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [creating, setCreating] = useState(false)

  async function load() {
    setLoading(true)
    setError(null)
    try {
      const res = await api.listNotes()
      setNotes(res.notes.filter((n) => !n.deleted_at))
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError("Couldn't load notes.")
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    void load()
  }, [])

  async function onNew() {
    setCreating(true)
    const payload = {
      title: 'Untitled',
      body: '',
      updated_at: nowIso(),
      updated_by_device_id: 'pwa',
    }
    try {
      const note = await api.createNote(payload)
      navigate(`/notes/${note.id}`)
    } catch (err) {
      if (isNetworkError(err)) {
        const id = crypto.randomUUID()
        await enqueueMutation({
          kind: 'note.create',
          path: '/v1/notes',
          method: 'POST',
          body: { ...payload, id },
        })
        setError("You're offline or the server is unreachable.")
      } else {
        setError('Something went wrong.')
      }
    } finally {
      setCreating(false)
    }
  }

  return (
    <div className="page stack">
      <div className="row-between">
        <h1>Notes</h1>
        <button type="button" className="btn btn-primary" disabled={creating} onClick={() => void onNew()}>
          New note
        </button>
      </div>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      {loading ? (
        <p className="muted">Loading…</p>
      ) : notes.length === 0 && !error ? (
        <div className="empty panel">
          <h2>No notes yet</h2>
          <p className="muted">Notes you dictate on Pocket will show up here.</p>
        </div>
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
    </div>
  )
}
