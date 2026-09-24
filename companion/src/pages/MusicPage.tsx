import { useEffect, useRef, useState } from 'react'
import { api, isNetworkError } from '../lib/api'
import type { MusicTrack } from '../lib/types'
import { relativeTime } from '../lib/utils'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

const MAX_BYTES = 2 * 1024 * 1024

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

export function MusicPage() {
  useDocumentTitle('Music')
  const inputRef = useRef<HTMLInputElement>(null)
  const [tracks, setTracks] = useState<MusicTrack[]>([])
  const [error, setError] = useState<string | null>(null)
  const [msg, setMsg] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [uploading, setUploading] = useState(false)

  async function load() {
    setLoading(true)
    setError(null)
    try {
      const res = await api.listMusic()
      setTracks(res.tracks)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError("Couldn't load music.")
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
    if (file.size > MAX_BYTES) {
      setError(`Keep each WAV under ${formatBytes(MAX_BYTES)} so Pocket can store it.`)
      return
    }
    const name = file.name || 'track.wav'
    if (!/\.wav$/i.test(name) && file.type && !file.type.includes('wav')) {
      setError('Only WAV files play on Pocket right now.')
      return
    }
    setUploading(true)
    try {
      const audio_b64 = await fileToBase64(file)
      await api.uploadMusic({
        title: name.replace(/\.wav$/i, ''),
        filename: name.endsWith('.wav') || name.endsWith('.WAV') ? name : `${name}.wav`,
        mime: file.type || 'audio/wav',
        audio_b64,
      })
      setMsg(`Uploaded ${name}. On Pocket: Home → Music → Sync from Companion.`)
      await load()
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else if (err && typeof err === 'object' && 'message' in err) setError(String((err as { message: string }).message))
      else setError("Couldn't upload that track.")
    } finally {
      setUploading(false)
      if (inputRef.current) inputRef.current.value = ''
    }
  }

  async function onDelete(id: string, title: string) {
    if (!window.confirm(`Remove “${title}” from your Pocket Cloud library?`)) return
    setError(null)
    try {
      await api.deleteMusic(id)
      setMsg('Removed from Cloud. Re-sync on Pocket to drop the local copy.')
      await load()
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError("Couldn't delete that track.")
    }
  }

  return (
    <div className="page stack">
      <div className="row-between">
        <h1>Music</h1>
        <button
          type="button"
          className="btn btn-primary"
          disabled={uploading}
          onClick={() => inputRef.current?.click()}
        >
          {uploading ? 'Uploading…' : 'Upload WAV'}
        </button>
        <input
          ref={inputRef}
          type="file"
          accept="audio/wav,.wav"
          hidden
          onChange={(e) => void onPick(e.target.files?.[0] ?? null)}
        />
      </div>

      <p className="muted">
        Upload WAV tracks here. On your Pocket, open Music and choose Sync from Companion — files save to the SD
        card when present, otherwise internal storage.
      </p>

      {msg ? <p className="muted">{msg}</p> : null}
      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      {loading ? (
        <p className="muted">Loading…</p>
      ) : tracks.length === 0 && !error ? (
        <div className="empty panel">
          <h2>No tracks yet</h2>
          <p className="muted">Upload a short WAV to play through Pocket’s speaker.</p>
        </div>
      ) : (
        <ul className="list">
          {tracks.map((t) => (
            <li key={t.id} className="row-between" style={{ alignItems: 'flex-start', gap: '1rem' }}>
              <div>
                <div className="list-title">{t.title || t.filename}</div>
                <div className="list-meta">
                  {formatBytes(t.size_bytes || t.size || 0)}
                  {t.created_at ? ` · ${relativeTime(t.created_at)}` : null}
                </div>
              </div>
              <button type="button" className="btn" onClick={() => void onDelete(t.id, t.title || t.filename)}>
                Remove
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  )
}
