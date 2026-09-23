import { useEffect, useState } from 'react'
import { api, isNetworkError } from '../lib/api'
import type { Connector, ConnectorProvider } from '../lib/types'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'

const LABELS: Record<ConnectorProvider, string> = {
  drive: 'Google Drive',
  dropbox: 'Dropbox',
  onedrive: 'OneDrive',
}

const ORDER: ConnectorProvider[] = ['drive', 'dropbox', 'onedrive']

export function ConnectorsPage() {
  useDocumentTitle('Connectors')
  const [connectors, setConnectors] = useState<Connector[]>([])
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState<ConnectorProvider | null>(null)

  async function load() {
    setError(null)
    try {
      const res = await api.listConnectors()
      setConnectors(res.connectors)
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    }
  }

  useEffect(() => {
    void load()
  }, [])

  function statusFor(provider: ConnectorProvider): Connector['status'] {
    return connectors.find((c) => c.provider === provider)?.status ?? 'disconnected'
  }

  async function onConnect(provider: ConnectorProvider) {
    setBusy(provider)
    setError(null)
    try {
      const { url } = await api.connectProvider(provider)
      window.location.href = url
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
      setBusy(null)
    }
  }

  async function onDisconnect(provider: ConnectorProvider) {
    const label = LABELS[provider]
    if (!window.confirm(`Disconnect ${label}? Copies already saved there stay put.`)) return
    setBusy(provider)
    setError(null)
    try {
      await api.disconnectProvider(provider)
      await load()
    } catch (err) {
      if (isNetworkError(err)) setError("You're offline or the server is unreachable.")
      else setError('Something went wrong.')
    } finally {
      setBusy(null)
    }
  }

  return (
    <div className="page stack">
      <div className="stack-sm">
        <h1>Keep a copy where you already work</h1>
        <p className="muted">
          Export and sync copies of your Notes &amp; Lists to Google Drive, Dropbox, or OneDrive. This is not a
          document library inside Pocket.
        </p>
      </div>

      {error ? <ErrorState message={error} onRetry={() => void load()} /> : null}

      <ul className="list">
        {ORDER.map((provider) => {
          const connected = statusFor(provider) === 'connected'
          return (
            <li key={provider} className="list-item row-between">
              <div>
                <div className="list-title">{LABELS[provider]}</div>
                <div className="list-meta">{connected ? 'Connected' : 'Not connected'}</div>
              </div>
              {connected ? (
                <button
                  type="button"
                  className="btn btn-secondary"
                  disabled={busy === provider}
                  onClick={() => void onDisconnect(provider)}
                >
                  Disconnect
                </button>
              ) : (
                <button
                  type="button"
                  className="btn btn-primary"
                  disabled={busy === provider}
                  onClick={() => void onConnect(provider)}
                >
                  Connect
                </button>
              )}
            </li>
          )
        })}
      </ul>
    </div>
  )
}
