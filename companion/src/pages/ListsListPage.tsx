import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { api, isNetworkError, nowIso } from '../lib/api'
import { enqueueMutation } from '../lib/queue'
import type { PocketList } from '../lib/types'
import { relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function ListsListPage() {
  useDocumentTitle('Lists')
  const navigate = useNavigate()
  const [lists, setLists] = useState<PocketList[]>([])
  const [error, setError] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  async function load() {
    setLoading(true)
    setError(null)
    try {
      const res = await api.listLists()
      setLists(res.lists.filter((l) => !l.deleted_at))
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    void load()
  }, [])

  async function onNew() {
    const payload = {
      title: 'Untitled list',
      items: [] as [],
      updated_at: nowIso(),
      updated_by_device_id: 'pwa',
    }
    try {
      const list = await api.createList(payload)
      navigate(`/lists/${list.id}`)
    } catch (err) {
      if (isNetworkError(err)) {
        await enqueueMutation({
          kind: 'list.create',
          path: '/v1/lists',
          method: 'POST',
          body: { ...payload, id: crypto.randomUUID() },
        })
        setError("You're offline or the server is unreachable.")
      } else {
        setError('Something went wrong.')
      }
    }
  }

  return (
    <div className="page stack">
      <div className="row-between">
        <h1>Lists</h1>
        <button type="button" className="btn btn-primary" onClick={() => void onNew()}>
          New list
        </button>
      </div>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      {loading ? (
        <p className="muted">Loading…</p>
      ) : lists.length === 0 && !error ? (
        <div className="empty panel">
          <h2>No lists yet</h2>
        </div>
      ) : (
        <ul className="list">
          {lists.map((l) => (
            <li key={l.id}>
              <Link className="list-link" to={`/lists/${l.id}`}>
                <div className="list-title">{l.title || 'Untitled'}</div>
                <div className="list-meta">
                  {l.items.length} items · {relativeTime(l.updated_at)}
                </div>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  )
}
