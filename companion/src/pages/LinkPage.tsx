import { type FormEvent, useCallback, useEffect, useState } from 'react'
import { Link, Navigate, useNavigate, useSearchParams } from 'react-router-dom'
import { api, isNetworkError } from '../lib/api'
import { useAuth } from '../lib/auth'
import { ApiError } from '../lib/types'
import {
  DEVICE_PROVISION_BASE,
  getDeviceStatus,
  scanDeviceNetworks,
  sendDeviceWifi,
  type DeviceProvisionStatus,
} from '../lib/deviceWifi'
import { ErrorState } from '../components/ErrorState'
import { useDocumentTitle } from '../components/useDocumentTitle'
import { WordMark } from '../components/WordMark'

type WifiPhase = 'instructions' | 'connected' | 'sent' | 'error'

/**
 * Unified Link flow: SoftAP Wi‑Fi credentials, then claim pair code on the same account.
 * If Pocket is already on home Wi‑Fi, skip SoftAP and enter the pairing code.
 */
export function LinkPage() {
  useDocumentTitle('Link your Pocket')
  const { isAuthenticated, loading: authLoading } = useAuth()
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const codeFromQuery = (params.get('code') || '').toUpperCase()

  const [wifiPhase, setWifiPhase] = useState<WifiPhase>(codeFromQuery ? 'sent' : 'instructions')
  /** True when user skipped SoftAP because Pocket is already online. */
  const [skipWifi, setSkipWifi] = useState(Boolean(codeFromQuery))
  const [status, setStatus] = useState<DeviceProvisionStatus | null>(null)
  const [networks, setNetworks] = useState<string[]>([])
  const [ssid, setSsid] = useState('')
  const [wifiPassword, setWifiPassword] = useState('')
  const [wifiBusy, setWifiBusy] = useState(false)
  const [wifiError, setWifiError] = useState<string | null>(null)
  const [homeWifiReady, setHomeWifiReady] = useState(Boolean(codeFromQuery))

  const [code, setCode] = useState(codeFromQuery)
  const [pairBusy, setPairBusy] = useState(false)
  const [pairError, setPairError] = useState<string | null>(null)

  const tryReachDevice = useCallback(async () => {
    try {
      const st = await getDeviceStatus()
      setStatus(st)
      const sc = await scanDeviceNetworks()
      setNetworks(sc.networks || [])
      const preferred = st.preferred_ssid || sc.networks?.[0] || ''
      setSsid((prev) => prev || preferred)
      setSkipWifi(false)
      setWifiPhase('connected')
      setWifiError(null)
      return true
    } catch {
      if (!codeFromQuery) setWifiPhase('instructions')
      return false
    }
  }, [codeFromQuery])

  useEffect(() => {
    if (codeFromQuery || skipWifi || wifiPhase === 'sent') return
    let cancelled = false
    const tick = async () => {
      if (cancelled) return
      await tryReachDevice()
    }
    void tick()
    const id = window.setInterval(() => {
      void tick()
    }, 2500)
    return () => {
      cancelled = true
      window.clearInterval(id)
    }
  }, [tryReachDevice, wifiPhase, codeFromQuery, skipWifi])

  function goPairWithoutWifi() {
    setSkipWifi(true)
    setWifiError(null)
    setWifiPhase('sent')
    setHomeWifiReady(true)
  }

  async function onSendWifi(e: FormEvent) {
    e.preventDefault()
    if (!ssid.trim()) {
      setWifiError('Choose a network.')
      return
    }
    setWifiBusy(true)
    setWifiError(null)
    try {
      await sendDeviceWifi(ssid.trim(), wifiPassword)
      setSkipWifi(false)
      setWifiPhase('sent')
      setHomeWifiReady(false)
    } catch (err) {
      setWifiPhase('error')
      setWifiError(err instanceof Error ? err.message : 'Could not send Wi‑Fi to Pocket.')
    } finally {
      setWifiBusy(false)
    }
  }

  async function onClaim(e?: FormEvent) {
    e?.preventDefault()
    if (!isAuthenticated) {
      const returnTo = `/link${code ? `?code=${encodeURIComponent(code)}` : ''}`
      navigate(`/login?return_to=${encodeURIComponent(returnTo)}`)
      return
    }
    const trimmed = code.trim().toUpperCase()
    if (!trimmed) {
      setPairError("Enter the code shown on your Pocket.")
      return
    }
    setPairBusy(true)
    setPairError(null)
    try {
      try {
        const session = await api.getPairSession(trimmed)
        if (session.status === 'expired') {
          setPairError('This code has expired. Generate a new one on your Pocket.')
          setPairBusy(false)
          return
        }
        if (session.status === 'claimed') {
          setPairError('This Pocket is linked to another account. Unlink it there first.')
          setPairBusy(false)
          return
        }
      } catch (err) {
        if (err instanceof ApiError && err.status === 404) {
          setPairError(
            "We couldn't find that code. Wait until Pocket shows the pairing screen, then try again.",
          )
          setPairBusy(false)
          return
        }
        if (isNetworkError(err)) {
          setPairError("You're offline or the server is unreachable. Rejoin your home Wi‑Fi first.")
          setPairBusy(false)
          return
        }
      }

      const result = await api.claimPair(trimmed)
      navigate(`/devices/${result.device_id}/setup`, { replace: true })
    } catch (err) {
      if (isNetworkError(err)) {
        setPairError("You're offline or the server is unreachable.")
      } else if (err instanceof ApiError) {
        setPairError(err.message || "We couldn't find that code.")
      } else {
        setPairError('Something went wrong.')
      }
    } finally {
      setPairBusy(false)
    }
  }

  if (authLoading) {
    return (
      <div className="page">
        <p className="muted">Loading…</p>
      </div>
    )
  }

  const showPair = wifiPhase === 'sent' || Boolean(codeFromQuery)

  return (
    <div className="page stack" style={{ maxWidth: '28rem', paddingTop: '2rem' }}>
      <WordMark to="/" />
      <div className="stack-sm">
        <h1>Link your Pocket</h1>
        <p className="muted">
          {skipWifi || codeFromQuery
            ? 'Enter the pairing code from Pocket to link this account.'
            : 'First send your home Wi‑Fi password to Pocket, then enter the pairing code — or skip Wi‑Fi if it is already online.'}
        </p>
      </div>

      {!showPair ? (
        <>
          {wifiPhase === 'instructions' || wifiPhase === 'error' ? (
            <div className="panel stack">
              <ol className="stack-sm" style={{ paddingLeft: '1.2rem', margin: 0 }}>
                <li>
                  On this phone, join <strong>{status?.ap_ssid || 'Pocket-XXXX'}</strong> using the
                  password on Pocket.
                </li>
                <li>
                  Open{' '}
                  <a href={`${DEVICE_PROVISION_BASE}/`}>
                    {DEVICE_PROVISION_BASE}
                  </a>{' '}
                  (best on SoftAP — avoids browser blocks) and enter your <strong>home Wi‑Fi password</strong>.
                </li>
                <li>Or stay here and tap continue once joined.</li>
              </ol>
              {wifiError ? (
                <p role="alert" className="muted">
                  {wifiError}
                </p>
              ) : null}
              <a className="btn btn-primary btn-block" href={`${DEVICE_PROVISION_BASE}/`}>
                Open Pocket Wi‑Fi setup
              </a>
              <button
                type="button"
                className="btn btn-block"
                onClick={() => void tryReachDevice()}
              >
                I’ve joined — continue in this app
              </button>
              <button type="button" className="btn btn-block" onClick={goPairWithoutWifi}>
                Skip — Pocket is already online
              </button>
            </div>
          ) : null}

          {wifiPhase === 'connected' ? (
            <form className="panel stack" onSubmit={(e) => void onSendWifi(e)}>
              <p className="muted">
                Connected to Pocket. Choose your home network and enter its password — this is step 1.
                Pairing code comes next on Pocket.
              </p>
              <div className="field">
                <label htmlFor="ssid">Home network</label>
                {networks.length > 0 ? (
                  <select id="ssid" value={ssid} onChange={(e) => setSsid(e.target.value)} required>
                    {networks.map((n) => (
                      <option key={n} value={n}>
                        {n}
                      </option>
                    ))}
                  </select>
                ) : (
                  <input
                    id="ssid"
                    value={ssid}
                    onChange={(e) => setSsid(e.target.value)}
                    placeholder="Network name"
                    required
                  />
                )}
              </div>
              <div className="field">
                <label htmlFor="password">Home Wi‑Fi password</label>
                <input
                  id="password"
                  type="password"
                  autoComplete="current-password"
                  value={wifiPassword}
                  onChange={(e) => setWifiPassword(e.target.value)}
                />
              </div>
              {wifiError ? (
                <p role="alert" className="muted">
                  {wifiError}
                </p>
              ) : null}
              <button type="submit" className="btn btn-primary btn-block" disabled={wifiBusy}>
                {wifiBusy ? 'Sending…' : 'Send password & continue'}
              </button>
              <button type="button" className="btn btn-block" onClick={goPairWithoutWifi}>
                Skip — Pocket is already online
              </button>
            </form>
          ) : null}
        </>
      ) : (
        <div className="panel stack">
          <p>
            {skipWifi || codeFromQuery
              ? 'Enter the pairing code shown on Pocket to finish linking.'
              : 'Pocket is joining Wi‑Fi. Rejoin your home network on this phone, wait for the pairing code on Pocket, then link below.'}
          </p>
          {skipWifi && !codeFromQuery ? (
            <button
              type="button"
              className="btn btn-block"
              onClick={() => {
                setSkipWifi(false)
                setWifiPhase('instructions')
                setHomeWifiReady(false)
              }}
            >
              Back to Wi‑Fi setup
            </button>
          ) : null}
          {!homeWifiReady ? (
            <button type="button" className="btn btn-primary btn-block" onClick={() => setHomeWifiReady(true)}>
              I’m back on home Wi‑Fi — enter pairing code
            </button>
          ) : !isAuthenticated ? (
            <Link
              className="btn btn-primary btn-block"
              to={`/login?return_to=${encodeURIComponent(`/link${code ? `?code=${encodeURIComponent(code)}` : ''}`)}`}
            >
              Sign in to finish linking
            </Link>
          ) : (
            <form className="stack" onSubmit={(e) => void onClaim(e)}>
              {!codeFromQuery ? (
                <div className="field">
                  <label htmlFor="code">Pairing code from Pocket</label>
                  <input
                    id="code"
                    name="code"
                    value={code}
                    onChange={(e) => setCode(e.target.value.toUpperCase())}
                    maxLength={8}
                    autoCapitalize="characters"
                    autoCorrect="off"
                    spellCheck={false}
                    placeholder="XXXXXXXX"
                    autoFocus
                  />
                </div>
              ) : (
                <p className="chip">Code · {code}</p>
              )}
              {pairError ? (
                pairError.includes('unreachable') ? (
                  <ErrorState message={pairError} onRetry={() => void onClaim()} />
                ) : (
                  <p role="alert" className="muted">
                    {pairError}
                  </p>
                )
              ) : null}
              <button type="submit" className="btn btn-primary btn-block" disabled={pairBusy}>
                {pairBusy ? 'Linking…' : 'Link this Pocket'}
              </button>
            </form>
          )}
        </div>
      )}
    </div>
  )
}

/** Legacy routes redirect into unified Link. */
export function WifiSetupRedirect() {
  return <Navigate to="/link" replace />
}

export function PairRedirect() {
  const [params] = useSearchParams()
  const code = params.get('code')
  const to = code ? `/link?code=${encodeURIComponent(code)}` : '/link'
  return <Navigate to={to} replace />
}
