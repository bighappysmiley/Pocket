import { useEffect, useState } from 'react'
import { api, isNetworkError } from '../lib/api'
import type { BackupMeta } from '../lib/types'
import { formatDate } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

export function BackupPage() {
  useDocumentTitle('Backup & restore')
  const [backups, setBackups] = useState<BackupMeta[]>([])
  const [error, setError] = useState<string | null>(null)
  const [msg, setMsg] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const [confirmId, setConfirmId] = useState<string | null>(null)

  async function load() {
    setError(null)
    try {
      const res = await api.listBackups()
      setBackups(res.backups)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  useEffect(() => {
    void load()
  }, [])

  async function onCreate() {
    setBusy(true)
    setError(null)
    setMsg(null)
    try {
      await api.createBackup()
      setMsg('Backup created.')
      await load()
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setBusy(false)
    }
  }

  async function onDownload() {
    setBusy(true)
    setError(null)
    try {
      const { url } = await api.downloadLatestBackup()
      window.location.href = url
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setBusy(false)
    }
  }

  async function onRestore(id: string) {
    setBusy(true)
    setError(null)
    try {
      await api.restoreBackup(id)
      setMsg('Backup restored. Devices will update when online.')
      setConfirmId(null)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError("Couldn't restore backup.")
    } finally {
      setBusy(false)
    }
  }

  const latest = backups[0]

  return (
    <div className="page stack">
      <h1>Backup &amp; restore</h1>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}
      {msg ? <p className="muted">{msg}</p> : null}

      <div className="actions">
        <button type="button" className="btn btn-primary btn-block" disabled={busy} onClick={() => void onCreate()}>
          Create backup
        </button>
        <button type="button" className="btn btn-secondary btn-block" disabled={busy || !latest} onClick={() => void onDownload()}>
          Download latest backup
        </button>
        {latest ? (
          <button type="button" className="btn btn-secondary btn-block" disabled={busy} onClick={() => setConfirmId(latest.id)}>
            Restore…
          </button>
        ) : null}
      </div>

      {backups.length > 0 ? (
        <ul className="list">
          {backups.map((b) => (
            <li key={b.id} className="list-item">
              <div className="list-title">{formatDate(b.created_at)}</div>
              <div className="list-meta">
                {b.note_count} notes · {b.list_count} lists
              </div>
            </li>
          ))}
        </ul>
      ) : null}

      {confirmId ? (
        <div className="panel stack">
          <p>Restore replaces Cloud Notes &amp; Lists with the backup. Your device will sync after restore.</p>
          <div className="actions-row">
            <button type="button" className="btn btn-primary" disabled={busy} onClick={() => void onRestore(confirmId)}>
              Restore
            </button>
            <button type="button" className="btn btn-secondary" onClick={() => setConfirmId(null)}>
              Cancel
            </button>
          </div>
        </div>
      ) : null}
    </div>
  )
}
