/** Talk to Pocket SoftAP provision portal at 192.168.4.1 while the phone is joined to Pocket-XXXX. */

export const DEVICE_PROVISION_BASE = 'http://192.168.4.1'

export type DeviceProvisionStatus = {
  ok: boolean
  provisioning: boolean
  ap_ssid: string
  preferred_ssid: string
  credentials_received: boolean
}

export type DeviceScanResult = {
  ok: boolean
  networks: string[]
}

async function deviceFetch<T>(path: string, init?: RequestInit): Promise<T> {
  const ctrl = new AbortController()
  const timer = window.setTimeout(() => ctrl.abort(), 8000)
  try {
    const res = await fetch(`${DEVICE_PROVISION_BASE}${path}`, {
      ...init,
      signal: ctrl.signal,
      headers: {
        Accept: 'application/json',
        ...(init?.body ? { 'Content-Type': 'application/json' } : {}),
        ...init?.headers,
      },
    })
    const data = (await res.json().catch(() => ({}))) as T & { message?: string }
    if (!res.ok) {
      throw new Error((data as { message?: string }).message || 'Pocket did not accept that request.')
    }
    return data
  } finally {
    window.clearTimeout(timer)
  }
}

export function getDeviceStatus() {
  return deviceFetch<DeviceProvisionStatus>('/api/status')
}

export function scanDeviceNetworks() {
  return deviceFetch<DeviceScanResult>('/api/scan')
}

export function sendDeviceWifi(ssid: string, password: string) {
  return deviceFetch<{ ok: boolean; message?: string }>('/api/wifi', {
    method: 'POST',
    body: JSON.stringify({ ssid, password }),
  })
}
