import { type FormEvent, useEffect, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api, isNetworkError, nowIso } from '../lib/api'
import { enqueueMutation } from '../lib/queue'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function NoteDetailPage() {
  const { id = '' } = useParams()
  const navigate = useNavigate()
  const [title, setTitle] = useState('')
  const [body, setBody] = useState('')
  const [sync, setSync] = useState<'idle' | 'saving' | 'synced' | 'retry'>('idle')
  const [error, setError] = useState<string | null>(null)
  const [confirmDelete, setConfirmDelete] = useState(false)
  const [shareUrl, setShareUrl] = useState<string | null>(null)
  const [shareMsg, setShareMsg] = useState<string | null>(null)

  useDocumentTitle(title || 'Note')

  async function load() {
    setError(null)
    try {
      const note = await api.getNote(id)
      setTitle(note.title)
      setBody(note.body)
      setSync('synced')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  useEffect(() => {
    void load()
  }, [id])

  async function onSave(e?: FormEvent) {
    e?.preventDefault()
    setSync('saving')
    setError(null)
    const payload = {
      title,
      body,
      updated_at: nowIso(),
      updated_by_device_id: 'pwa',
    }
    try {
      const note = await api.updateNote(id, payload)
      setTitle(note.title)
      setBody(note.body)
      setSync('synced')
    } catch (err) {
      if (isNetworkError(err)) {
        await enqueueMutation({
          kind: 'note.update',
          path: `/v1/notes/${id}`,
          method: 'PATCH',
          body: payload,
        })
        setSync('retry')
        setError("Couldn't sync. Will retry.")
      } else {
        setSync('retry')
        setError('Something went wrong.')
      }
    }
  }

  async function onDelete() {
    const payload = { updated_at: nowIso(), updated_by_device_id: 'pwa' }
    try {
      await api.deleteNote(id, payload)
      navigate('/notes', { replace: true })
    } catch (err) {
      if (isNetworkError(err)) {
        await enqueueMutation({
          kind: 'note.delete',
          path: `/v1/notes/${id}`,
          method: 'DELETE',
          body: payload,
        })
        setError("Couldn't sync. Will retry.")
      } else {
        setError('Something went wrong.')
      }
    }
  }

  async function onShare() {
    setShareMsg(null)
    try {
      const res = await api.createShareLink(id)
      setShareUrl(res.url)
      setShareMsg('Link created. Anyone with the link can view this note.')
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  async function onCopy() {
    if (!shareUrl) return
    try {
      await navigator.clipboard.writeText(shareUrl)
      setShareMsg('Copied.')
    } catch {
      setShareMsg(shareUrl)
    }
  }

  async function onRevoke() {
    try {
      await api.revokeShareLink(id)
      setShareUrl(null)
      setShareMsg(null)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  return (
    <div className="page stack">
      <div className="row-between">
        <Link to="/notes" className="btn btn-ghost">
          Back
        </Link>
        <span className="muted" style={{ fontSize: '0.875rem' }}>
          {sync === 'saving' ? 'Saving…' : sync === 'synced' ? 'Synced with Pocket' : sync === 'retry' ? "Couldn't sync. Will retry." : null}
        </span>
      </div>

      {error ? (
        error.includes('unreachable') || error.includes('retry') ? (
          <ErrorState message={error.includes('retry') ? error : error} onRetry={() => void onSave()} />
        ) : (
          <p role="alert" className="muted">
            {error}
          </p>
        )
      ) : null}

      <form className="stack" onSubmit={(e) => void onSave(e)}>
        <div className="field">
          <label htmlFor="title" className="sr-only">
            Title
          </label>
          <input id="title" value={title} onChange={(e) => setTitle(e.target.value)} placeholder="Title" />
        </div>
        <div className="field">
          <label htmlFor="body" className="sr-only">
            Body
          </label>
          <textarea id="body" value={body} onChange={(e) => setBody(e.target.value)} placeholder="Write…" />
        </div>
        <div className="actions-row">
          <button type="submit" className="btn btn-primary">
            Save
          </button>
          <button type="button" className="btn btn-secondary" onClick={() => void onShare()}>
            Share link
          </button>
          <button type="button" className="btn btn-danger" onClick={() => setConfirmDelete(true)}>
            Delete
          </button>
        </div>
      </form>

      {shareMsg ? (
        <div className="panel stack-sm">
          <p>{shareMsg}</p>
          {shareUrl ? (
            <div className="actions-row">
              <button type="button" className="btn btn-secondary" onClick={() => void onCopy()}>
                Copy link
              </button>
              <button type="button" className="btn btn-ghost" onClick={() => void onRevoke()}>
                Revoke link
              </button>
            </div>
          ) : null}
        </div>
      ) : null}

      {confirmDelete ? (
        <div className="panel stack">
          <p>Delete this note on Pocket and in Pocket Cloud?</p>
          <div className="actions-row">
            <button type="button" className="btn btn-danger" onClick={() => void onDelete()}>
              Delete
            </button>
            <button type="button" className="btn btn-secondary" onClick={() => setConfirmDelete(false)}>
              Cancel
            </button>
          </div>
        </div>
      ) : null}
    </div>
  )
}
