import { useEffect, useRef, useState } from 'react'
import { api, isNetworkError } from '../lib/api'
import type { Book } from '../lib/types'
import { relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

const MAX_UPLOAD_BYTES = 20 * 1024 * 1024

function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`
  return `${(n / (1024 * 1024)).toFixed(2)} MB`
}

async function fileToBase64(file: File): Promise<string> {
  const buf = await file.arrayBuffer()
  const bytes = new Uint8Array(buf)
  let binary = ''
  const chunk = 0x8000
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk))
  }
  return btoa(binary)
}

function guessFormat(name: string): 'epub' | 'txt' {
  return /\.epub$/i.test(name) ? 'epub' : 'txt'
}

export function BooksPage() {
  useDocumentTitle('Reading')
  const inputRef = useRef<HTMLInputElement>(null)
  const [books, setBooks] = useState<Book[]>([])
  const [error, setError] = useState<string | null>(null)
  const [msg, setMsg] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [uploading, setUploading] = useState(false)

  async function load() {
    setLoading(true)
    setError(null)
    try {
      const res = await api.listBooks()
      setBooks(res.books)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError("Couldn't load your library.")
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    void load()
  }, [])

  async function onPick(file: File | null) {
    if (!file) return
    setMsg(null)
    setError(null)
    if (file.size > MAX_UPLOAD_BYTES) {
      setError(`Keep each book under ${formatBytes(MAX_UPLOAD_BYTES)}.`)
      return
    }
    const format = guessFormat(file.name)
    if (!/\.(epub|txt)$/i.test(file.name)) {
      setError('Upload an EPUB or plain-text (.txt) file.')
      return
    }
    setUploading(true)
    try {
      const file_b64 = await fileToBase64(file)
      await api.uploadBook({
        title: file.name.replace(/\.(epub|txt)$/i, ''),
        filename: file.name,
        format,
        file_b64,
      })
      setMsg(`Uploaded ${file.name}. On Pocket: Home → Reading → Sync from Companion.`)
      await load()
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else if (err && typeof err === 'object' && 'message' in err) setError(String((err as { message: string }).message))
      else setError("Couldn't upload that book.")
    } finally {
      setUploading(false)
      if (inputRef.current) inputRef.current.value = ''
    }
  }

  async function onDelete(id: string, title: string) {
    if (!window.confirm(`Remove “${title}” from your Pocket Cloud library?`)) return
    setError(null)
    try {
      await api.deleteBook(id)
      setMsg('Removed from Cloud. Re-sync on Pocket to drop the local copy.')
      await load()
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError("Couldn't delete that book.")
    }
  }

  return (
    <div className="page stack">
      <div className="row-between">
        <h1>Reading</h1>
        <button
          type="button"
          className="btn btn-primary"
          disabled={uploading}
          onClick={() => inputRef.current?.click()}
        >
          {uploading ? 'Uploading…' : 'Upload book'}
        </button>
        <input
          ref={inputRef}
          type="file"
          accept=".epub,.txt,application/epub+zip,text/plain"
          hidden
          onChange={(e) => void onPick(e.target.files?.[0] ?? null)}
        />
      </div>

      <p className="muted">
        EPUB or plain text, up to {formatBytes(MAX_UPLOAD_BYTES)}. Pocket reads a plain-text
        version of your EPUBs — pagination stays crisp on e-ink. Large libraries sync best with a
        microSD card in Pocket.
      </p>

      {msg ? <p className="muted">{msg}</p> : null}
      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      {loading ? (
        <p className="muted">Loading…</p>
      ) : books.length === 0 && !error ? (
        <div className="empty panel">
          <h2>No books yet</h2>
          <p className="muted">Upload an EPUB or TXT to read on Pocket.</p>
        </div>
      ) : (
        <ul className="list">
          {books.map((b) => (
            <li key={b.id} className="row-between" style={{ alignItems: 'flex-start', gap: '1rem' }}>
              <div>
                <div className="list-title">{b.title || b.filename}</div>
                <div className="list-meta">
                  {b.author ? `${b.author} · ` : ''}
                  {(b.format || 'txt').toUpperCase()} · {formatBytes(b.size_bytes || b.size || 0)}
                  {b.created_at ? ` · ${relativeTime(b.created_at)}` : null}
                </div>
              </div>
              <button type="button" className="btn" onClick={() => void onDelete(b.id, b.title || b.filename)}>
                Remove
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  )
}
