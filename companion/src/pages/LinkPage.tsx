import { type FormEvent, useCallback, useEffect, useMemo, useState } from 'react'
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
/** How Pocket gets internet — home router or this phone’s Personal Hotspot (cell data). */
type NetMode = 'home' | 'phone'

function looksLikeHotspot(name: string): boolean {
  const n = name.toLowerCase()
  return (
    n.includes('iphone') ||
    n.includes('ipad') ||
    n.includes('android') ||
    n.includes('hotspot') ||
    n.includes('galaxy') ||
    n.includes('pixel') ||
    n.startsWith('oneplus') ||
    n.includes("'s iphone") ||
    n.includes('’s iphone')
  )
}

function sortNetworks(nets: string[], preferHotspot: boolean): string[] {
  const uniq = [...new Set(nets.filter(Boolean))]
  return uniq.sort((a, b) => {
    const ah = looksLikeHotspot(a) ? 0 : 1
    const bh = looksLikeHotspot(b) ? 0 : 1
    if (preferHotspot && ah !== bh) return ah - bh
    if (!preferHotspot && ah !== bh) return bh - ah
    return a.localeCompare(b)
  })
}

/**
 * Unified Link flow: SoftAP credentials (home Wi‑Fi or phone hotspot), then claim pair code.
 * Phone hotspot = Personal Hotspot / cell tether — Pocket STA to the phone, not Bluetooth PAN
 * (Web Bluetooth cannot do classic PAN from a PWA).
 */
export function LinkPage() {
  useDocumentTitle('Link your Pocket')
  const { isAuthenticated, loading: authLoading } = useAuth()
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const codeFromQuery = (params.get('code') || '').toUpperCase()
  const modeFromQuery = params.get('mode') === 'phone' ? 'phone' : null

  const [wifiPhase, setWifiPhase] = useState<WifiPhase>(codeFromQuery ? 'sent' : 'instructions')
  /** True when user skipped SoftAP because Pocket is already online. */
  const [skipWifi, setSkipWifi] = useState(Boolean(codeFromQuery))
  const [netMode, setNetMode] = useState<NetMode>(modeFromQuery || 'home')
  const [status, setStatus] = useState<DeviceProvisionStatus | null>(null)
  const [networks, setNetworks] = useState<string[]>([])
  const [ssid, setSsid] = useState('')
  const [manualSsid, setManualSsid] = useState(false)
  const [wifiPassword, setWifiPassword] = useState('')
  const [wifiBusy, setWifiBusy] = useState(false)
  const [wifiError, setWifiError] = useState<string | null>(null)
  const [onlineReady, setOnlineReady] = useState(Boolean(codeFromQuery))

  const [code, setCode] = useState(codeFromQuery)
  const [pairBusy, setPairBusy] = useState(false)
  const [pairError, setPairError] = useState<string | null>(null)

  const orderedNetworks = useMemo(
    () => sortNetworks(networks, netMode === 'phone'),
    [networks, netMode],
  )

  const tryReachDevice = useCallback(async () => {
    try {
      const st = await getDeviceStatus()
      setStatus(st)
      const sc = await scanDeviceNetworks()
      const nets = sc.networks || []
      setNetworks(nets)
      const sorted = sortNetworks(nets, netMode === 'phone')
      const preferred =
        (netMode === 'phone' ? sorted.find(looksLikeHotspot) : undefined) ||
        st.preferred_ssid ||
        sorted[0] ||
        ''
      setSsid((prev) => prev || preferred)
      setSkipWifi(false)
      setWifiPhase('connected')
      setWifiError(null)
      return true
    } catch {
      if (!codeFromQuery) setWifiPhase('instructions')
      return false
    }
  }, [codeFromQuery, netMode])

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
    setOnlineReady(true)
  }

  function choosePhoneData() {
    setNetMode('phone')
    setManualSsid(false)
    setSsid('')
    setWifiPassword('')
    setWifiError(null)
    setWifiPhase('instructions')
  }

  function chooseHomeWifi() {
    setNetMode('home')
    setManualSsid(false)
    setSsid('')
    setWifiPassword('')
    setWifiError(null)
    setWifiPhase('instructions')
  }

  async function onSendWifi(e: FormEvent) {
    e.preventDefault()
    if (!ssid.trim()) {
      setWifiError(netMode === 'phone' ? 'Enter your Personal Hotspot name.' : 'Choose a network.')
      return
    }
    setWifiBusy(true)
    setWifiError(null)
    try {
      await sendDeviceWifi(ssid.trim(), wifiPassword)
      setSkipWifi(false)
      setWifiPhase('sent')
      setOnlineReady(false)
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
      setPairError('Enter the code shown on your Pocket.')
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
          setPairError(
            netMode === 'phone'
              ? "You're offline. Leave Pocket Wi‑Fi, turn Personal Hotspot back on, and use cell data."
              : "You're offline or the server is unreachable. Rejoin your home Wi‑Fi first.",
          )
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
  const phone = netMode === 'phone'

  return (
    <div className="page stack" style={{ maxWidth: '28rem', paddingTop: '2rem' }}>
      <WordMark to="/" />
      <div className="stack-sm">
        <h1>Link your Pocket</h1>
        <p className="muted">
          {skipWifi || codeFromQuery
            ? 'Enter the pairing code from Pocket to link this account.'
            : phone
              ? 'Share this phone’s cell data via Personal Hotspot — no home Wi‑Fi required.'
              : 'Send a Wi‑Fi password to Pocket (home network or phone hotspot), then enter the pairing code.'}
        </p>
      </div>

      {!showPair ? (
        <>
          {wifiPhase === 'instructions' || wifiPhase === 'error' ? (
            <div className="panel stack">
              <div className="stack-sm" style={{ display: 'flex', gap: '0.5rem', flexWrap: 'wrap' }}>
                <button
                  type="button"
                  className={phone ? 'btn btn-block' : 'btn btn-primary btn-block'}
                  onClick={chooseHomeWifi}
                  style={{ flex: '1 1 8rem' }}
                >
                  Home Wi‑Fi
                </button>
                <button
                  type="button"
                  className={phone ? 'btn btn-primary btn-block' : 'btn btn-block'}
                  onClick={choosePhoneData}
                  style={{ flex: '1 1 8rem' }}
                >
                  Use phone data
                </button>
              </div>

              {phone ? (
                <ol className="stack-sm" style={{ paddingLeft: '1.2rem', margin: 0 }}>
                  <li>
                    Note your <strong>Personal Hotspot</strong> name and password (Settings → Personal
                    Hotspot / Mobile Hotspot). On iPhone, turn on <strong>Maximize Compatibility</strong>{' '}
                    if shown.
                  </li>
                  <li>
                    Join <strong>{status?.ap_ssid || 'Pocket-XXXX'}</strong> with the password on Pocket
                    (this briefly leaves cell data).
                  </li>
                  <li>
                    Open{' '}
                    <a href={`${DEVICE_PROVISION_BASE}/?mode=phone`}>
                      {DEVICE_PROVISION_BASE}
                    </a>{' '}
                    and choose your hotspot — or continue here.
                  </li>
                  <li>
                    After Pocket accepts it, leave Pocket Wi‑Fi and turn Personal Hotspot back on so
                    Pocket can use your cell data.
                  </li>
                </ol>
              ) : (
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
                    and enter your <strong>home Wi‑Fi</strong> password — or use{' '}
                    <strong>Use phone data</strong> above for Personal Hotspot.
                  </li>
                  <li>Or stay here and tap continue once joined.</li>
                </ol>
              )}

              {wifiError ? (
                <p role="alert" className="muted">
                  {wifiError}
                </p>
              ) : null}
              <a
                className="btn btn-primary btn-block"
                href={`${DEVICE_PROVISION_BASE}/${phone ? '?mode=phone' : ''}`}
              >
                Open Pocket Wi‑Fi setup
              </a>
              <button type="button" className="btn btn-block" onClick={() => void tryReachDevice()}>
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
                {phone
                  ? 'Connected to Pocket. Pick this phone’s Personal Hotspot (or type the name), enter the hotspot password, then leave Pocket Wi‑Fi and turn the hotspot back on.'
                  : 'Connected to Pocket. Choose your home network and enter its password — or switch to Use phone data for cell tether.'}
              </p>
              <div className="stack-sm" style={{ display: 'flex', gap: '0.5rem', flexWrap: 'wrap' }}>
                <button
                  type="button"
                  className={!phone ? 'btn btn-primary' : 'btn'}
                  onClick={chooseHomeWifi}
                >
                  Home Wi‑Fi
                </button>
                <button
                  type="button"
                  className={phone ? 'btn btn-primary' : 'btn'}
                  onClick={() => {
                    setNetMode('phone')
                    const hot = orderedNetworks.find(looksLikeHotspot)
                    if (hot) setSsid(hot)
                    else setManualSsid(true)
                  }}
                >
                  Use phone data
                </button>
              </div>
              <div className="field">
                <label htmlFor="ssid">{phone ? 'Personal Hotspot name' : 'Home network'}</label>
                {!manualSsid && orderedNetworks.length > 0 ? (
                  <>
                    <select id="ssid" value={ssid} onChange={(e) => setSsid(e.target.value)} required>
                      {orderedNetworks.map((n) => (
                        <option key={n} value={n}>
                          {n}
                          {looksLikeHotspot(n) ? ' · hotspot?' : ''}
                        </option>
                      ))}
                    </select>
                    <button
                      type="button"
                      className="btn btn-block"
                      style={{ marginTop: '0.5rem' }}
                      onClick={() => setManualSsid(true)}
                    >
                      Type network name instead
                    </button>
                  </>
                ) : (
                  <input
                    id="ssid"
                    value={ssid}
                    onChange={(e) => setSsid(e.target.value)}
                    placeholder={phone ? 'e.g. Jane’s iPhone' : 'Network name'}
                    required
                  />
                )}
              </div>
              <div className="field">
                <label htmlFor="password">{phone ? 'Hotspot password' : 'Home Wi‑Fi password'}</label>
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
                {wifiBusy ? 'Sending…' : phone ? 'Send hotspot & continue' : 'Send password & continue'}
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
              : phone
                ? 'Pocket is joining your Personal Hotspot. Leave Pocket Wi‑Fi, turn the hotspot back on (cell data), wait for the pairing code on Pocket, then link below.'
                : 'Pocket is joining Wi‑Fi. Rejoin your home network on this phone, wait for the pairing code on Pocket, then link below.'}
          </p>
          {skipWifi && !codeFromQuery ? (
            <button
              type="button"
              className="btn btn-block"
              onClick={() => {
                setSkipWifi(false)
                setWifiPhase('instructions')
                setOnlineReady(false)
              }}
            >
              Back to Wi‑Fi setup
            </button>
          ) : null}
          {!onlineReady ? (
            <button type="button" className="btn btn-primary btn-block" onClick={() => setOnlineReady(true)}>
              {phone
                ? 'Hotspot is on — enter pairing code'
                : 'I’m back on home Wi‑Fi — enter pairing code'}
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
                pairError.includes('unreachable') || pairError.includes('offline') ? (
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
