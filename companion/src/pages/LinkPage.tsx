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

type WifiPhase = 'form' | 'sent' | 'error'
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

function companionReturnUrl(addNetwork: boolean, phone: boolean): string {
  const base = `${window.location.origin}${import.meta.env.BASE_URL || '/'}`.replace(/\/?$/, '/')
  const path = addNetwork ? 'link?add=1&wifi=1' : `link?wifi=1${phone ? '&mode=phone' : ''}`
  return `${base}${path}`
}

/**
 * Link flow — credentials stay in Pocket Companion.
 * Linked devices: Cloud pushes Wi‑Fi (no SoftAP hop).
 * First setup: SoftAP auto-handoff with credentials already entered here (no typing at 192.168.4.1).
 */
export function LinkPage() {
  const { isAuthenticated, loading: authLoading } = useAuth()
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const codeFromQuery = (params.get('code') || '').toUpperCase()
  const modeFromQuery = params.get('mode') === 'phone' ? 'phone' : null
  const addNetwork = params.get('add') === '1'
  const deviceId = params.get('device') || ''
  const wifiDone = params.get('wifi') === '1'
  useDocumentTitle(addNetwork ? 'Add Wi‑Fi' : 'Link Pocket Version 1')

  const [wifiPhase, setWifiPhase] = useState<WifiPhase>(
    codeFromQuery || wifiDone ? 'sent' : 'form',
  )
  const [skipWifi, setSkipWifi] = useState(Boolean(codeFromQuery || wifiDone))
  const [netMode, setNetMode] = useState<NetMode>(modeFromQuery || 'home')
  const [status, setStatus] = useState<DeviceProvisionStatus | null>(null)
  const [networks, setNetworks] = useState<string[]>([])
  const [ssid, setSsid] = useState('')
  const [manualSsid, setManualSsid] = useState(true)
  const [wifiPassword, setWifiPassword] = useState('')
  const [wifiBusy, setWifiBusy] = useState(false)
  const [wifiError, setWifiError] = useState<string | null>(null)
  const [onlineReady, setOnlineReady] = useState(Boolean(codeFromQuery || wifiDone))
  const [softReachable, setSoftReachable] = useState(false)

  const [code, setCode] = useState(codeFromQuery)
  const [pairBusy, setPairBusy] = useState(false)
  const [pairError, setPairError] = useState<string | null>(null)

  const orderedNetworks = useMemo(() => {
    const uniq = [...new Set(networks.filter(Boolean))]
    return uniq.sort((a, b) => {
      const ah = looksLikeHotspot(a) ? 0 : 1
      const bh = looksLikeHotspot(b) ? 0 : 1
      if (netMode === 'phone' && ah !== bh) return ah - bh
      if (netMode !== 'phone' && ah !== bh) return bh - ah
      return a.localeCompare(b)
    })
  }, [networks, netMode])

  const tryReachDevice = useCallback(async () => {
    try {
      const st = await getDeviceStatus()
      setStatus(st)
      const sc = await scanDeviceNetworks()
      const nets = sc.networks || []
      setNetworks(nets)
      setSoftReachable(true)
      setManualSsid(nets.length === 0)
      const sorted = [...nets].sort((a, b) => a.localeCompare(b))
      const preferred =
        (netMode === 'phone' ? sorted.find(looksLikeHotspot) : undefined) ||
        st.preferred_ssid ||
        sorted[0] ||
        ''
      setSsid((prev) => prev || preferred)
      return true
    } catch {
      setSoftReachable(false)
      return false
    }
  }, [netMode])

  useEffect(() => {
    if (codeFromQuery || skipWifi || wifiPhase === 'sent') return
    void tryReachDevice()
    const id = window.setInterval(() => {
      void tryReachDevice()
    }, 4000)
    return () => window.clearInterval(id)
  }, [tryReachDevice, wifiPhase, codeFromQuery, skipWifi])

  function goPairWithoutWifi() {
    setSkipWifi(true)
    setWifiError(null)
    setWifiPhase('sent')
    setOnlineReady(true)
  }

  async function onSendWifi(e: FormEvent) {
    e.preventDefault()
    if (!ssid.trim()) {
      setWifiError(netMode === 'phone' ? 'Enter your Personal Hotspot name.' : 'Enter a network name.')
      return
    }
    setWifiBusy(true)
    setWifiError(null)
    try {
      // Linked device + online path: Cloud relay — stay fully in Companion (no SoftAP).
      if (addNetwork && (deviceId || isAuthenticated)) {
        let id = deviceId
        if (!id) {
          const list = await api.listDevices()
          id = list.devices[0]?.id || list.devices[0]?.device_id || ''
        }
        if (!id) {
          setWifiError('Link a Pocket first, then add Wi‑Fi from Devices.')
          setWifiPhase('error')
          return
        }
        await api.queueDeviceWifi(id, { ssid: ssid.trim(), password: wifiPassword })
        setSkipWifi(true)
        setWifiPhase('sent')
        setOnlineReady(true)
        return
      }

      // SoftAP reachable (local HTTP / cleartext): send in-app.
      if (softReachable) {
        await sendDeviceWifi(ssid.trim(), wifiPassword)
        setSkipWifi(false)
        setWifiPhase('sent')
        setOnlineReady(addNetwork)
        return
      }

      // HTTPS Pages: hand credentials to SoftAP portal via query (auto-submit), then return here.
      // User never types at 192.168.4.1 — form stays in Companion.
      const ret = encodeURIComponent(companionReturnUrl(addNetwork, netMode === 'phone'))
      const q = new URLSearchParams({
        ssid: ssid.trim(),
        password: wifiPassword,
        return: companionReturnUrl(addNetwork, netMode === 'phone'),
      })
      if (netMode === 'phone') q.set('mode', 'phone')
      window.location.href = `${DEVICE_PROVISION_BASE}/?${q.toString()}&return=${ret}`
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
              ? "You're offline. Turn Personal Hotspot back on and use cell data."
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
        <h1>{addNetwork ? 'Add a Wi‑Fi network' : 'Link Pocket Version 1'}</h1>
        <p className="muted">
          {skipWifi || codeFromQuery
            ? 'Enter the pairing code from Pocket to link this Pocket Cloud account.'
            : addNetwork
              ? 'Enter the network here. If Pocket is already online, we send it through Pocket Cloud — no SoftAP hop.'
              : phone
                ? 'Enter this phone’s Personal Hotspot name and password here in Companion.'
                : 'Enter home Wi‑Fi here in Companion. Pocket Version 1 joins that network, then you enter the pairing code.'}
        </p>
      </div>

      {!showPair ? (
        <form className="panel stack" onSubmit={(e) => void onSendWifi(e)}>
          <div className="stack-sm" style={{ display: 'flex', gap: '0.5rem', flexWrap: 'wrap' }}>
            <button
              type="button"
              className={!phone ? 'btn btn-primary' : 'btn'}
              onClick={() => {
                setNetMode('home')
                setWifiError(null)
              }}
            >
              Home Wi‑Fi
            </button>
            <button
              type="button"
              className={phone ? 'btn btn-primary' : 'btn'}
              onClick={() => {
                setNetMode('phone')
                setWifiError(null)
                const hot = orderedNetworks.find(looksLikeHotspot)
                if (hot) {
                  setSsid(hot)
                  setManualSsid(false)
                } else {
                  setManualSsid(true)
                }
              }}
            >
              Use phone data
            </button>
          </div>

          {softReachable ? (
            <p className="muted" style={{ margin: 0 }}>
              Pocket setup network reachable — sending stays in this app.
            </p>
          ) : addNetwork ? (
            <p className="muted" style={{ margin: 0 }}>
              Pocket should already be on Wi‑Fi. We queue the network in Pocket Cloud; Pocket picks it up
              within about a minute.
            </p>
          ) : (
            <p className="muted" style={{ margin: 0 }}>
              After you tap Send, briefly join the Pocket Wi‑Fi shown on the device so we can hand off the
              password you already entered — you return here automatically. You never type at an IP address.
            </p>
          )}

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
            {wifiBusy
              ? 'Sending…'
              : addNetwork
                ? 'Send to Pocket'
                : phone
                  ? 'Send hotspot & continue'
                  : 'Send password & continue'}
          </button>
          {!addNetwork ? (
            <button type="button" className="btn btn-block" onClick={goPairWithoutWifi}>
              Skip — Pocket is already online
            </button>
          ) : null}
          {status?.ap_ssid ? (
            <p className="muted" style={{ fontSize: '0.85rem', margin: 0 }}>
              Device SoftAP · {status.ap_ssid}
            </p>
          ) : null}
        </form>
      ) : (
        <div className="panel stack">
          <p>
            {skipWifi || codeFromQuery
              ? 'Enter the pairing code shown on Pocket to finish linking.'
              : addNetwork
                ? 'Pocket is saving that network and will use it when online. No pairing code needed.'
                : phone
                  ? 'Pocket is joining your Personal Hotspot. Turn the hotspot back on, wait for the pairing code on Pocket, then link below.'
                  : 'Pocket is joining Wi‑Fi. Wait for the pairing code on Pocket, then link below.'}
          </p>
          {addNetwork && !codeFromQuery ? (
            <Link className="btn btn-primary btn-block" to="/devices">
              Done — back to devices
            </Link>
          ) : (
            <>
              {skipWifi && !codeFromQuery ? (
                <button
                  type="button"
                  className="btn btn-block"
                  onClick={() => {
                    setSkipWifi(false)
                    setWifiPhase('form')
                    setOnlineReady(false)
                  }}
                >
                  Back to Wi‑Fi setup
                </button>
              ) : null}
              {!onlineReady ? (
                <button
                  type="button"
                  className="btn btn-primary btn-block"
                  onClick={() => setOnlineReady(true)}
                >
                  {phone
                    ? 'Hotspot is on — enter pairing code'
                    : 'I’m online — enter pairing code'}
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
            </>
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
