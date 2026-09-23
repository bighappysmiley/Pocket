import { type FormEvent, useEffect, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api, isNetworkError, nowIso } from '../lib/api'
import { enqueueMutation } from '../lib/queue'
import type { ListItem } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function ListDetailPage() {
  const { id = '' } = useParams()
  const navigate = useNavigate()
  const [title, setTitle] = useState('')
  const [items, setItems] = useState<ListItem[]>([])
  const [newItem, setNewItem] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [confirmDelete, setConfirmDelete] = useState(false)

  useDocumentTitle(title || 'List')

  async function load() {
    setError(null)
    try {
      const list = await api.getList(id)
      setTitle(list.title)
      setItems(list.items)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  useEffect(() => {
    void load()
  }, [id])

  async function persist(nextTitle: string, nextItems: ListItem[]) {
    const payload = {
      title: nextTitle,
      items: nextItems,
      updated_at: nowIso(),
      updated_by_device_id: 'pwa',
    }
    try {
      const list = await api.updateList(id, payload)
      setTitle(list.title)
      setItems(list.items)
    } catch (err) {
      if (isNetworkError(err)) {
        await enqueueMutation({
          kind: 'list.update',
          path: `/v1/lists/${id}`,
          method: 'PATCH',
          body: payload,
        })
        setError("Couldn't sync. Will retry.")
      } else {
        setError('Something went wrong.')
      }
    }
  }

  function toggle(itemId: string) {
    const next = items.map((it) =>
      it.id === itemId ? { ...it, checked: !it.checked, updated_at: nowIso() } : it,
    )
    setItems(next)
    void persist(title, next)
  }

  function onAdd(e: FormEvent) {
    e.preventDefault()
    const text = newItem.trim()
    if (!text) return
    const item: ListItem = {
      id: crypto.randomUUID(),
      text,
      checked: false,
      updated_at: nowIso(),
    }
    const next = [...items, item]
    setItems(next)
    setNewItem('')
    void persist(title, next)
  }

  function onSaveTitle(e: FormEvent) {
    e.preventDefault()
    void persist(title, items)
  }

  async function onDelete() {
    const payload = { updated_at: nowIso(), updated_by_device_id: 'pwa' }
    try {
      await api.deleteList(id, payload)
      navigate('/lists', { replace: true })
    } catch (err) {
      if (isNetworkError(err)) {
        await enqueueMutation({
          kind: 'list.delete',
          path: `/v1/lists/${id}`,
          method: 'DELETE',
          body: payload,
        })
        setError("Couldn't sync. Will retry.")
      } else {
        setError('Something went wrong.')
      }
    }
  }

  return (
    <div className="page stack">
      <div className="row-between">
        <Link to="/lists" className="btn btn-ghost">
          Back
        </Link>
        <button type="button" className="btn btn-danger" onClick={() => setConfirmDelete(true)}>
          Delete
        </button>
      </div>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      <form className="field" onSubmit={onSaveTitle}>
        <label htmlFor="list-title">Title</label>
        <input id="list-title" value={title} onChange={(e) => setTitle(e.target.value)} />
      </form>

      <div className="panel">
        {items.map((it) => (
          <label key={it.id} className="check-row">
            <input type="checkbox" checked={it.checked} onChange={() => toggle(it.id)} />
            <span style={{ textDecoration: it.checked ? 'line-through' : undefined, color: it.checked ? 'var(--muted)' : undefined }}>
              {it.text}
            </span>
          </label>
        ))}
        <form className="row" style={{ marginTop: '1rem' }} onSubmit={onAdd}>
          <input
            style={{ flex: 1 }}
            value={newItem}
            onChange={(e) => setNewItem(e.target.value)}
            placeholder="Add item"
            aria-label="Add item"
          />
          <button type="submit" className="btn btn-secondary">
            Add item
          </button>
        </form>
      </div>

      {confirmDelete ? (
        <div className="panel stack">
          <p>Delete this list on Pocket and in Pocket Cloud?</p>
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
