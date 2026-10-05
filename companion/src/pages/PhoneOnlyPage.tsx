import { Link, useSearchParams } from 'react-router-dom'
import { companionPublicUrl, isPhoneCompanionClient } from '../lib/utils'
import { useDocumentTitle } from '../components/useDocumentTitle'
import { WordMark } from '../components/WordMark'

function QrImg({ url }: { url: string }) {
  const src = `https://api.qrserver.com/v1/create-qr-code/?size=180x180&margin=8&data=${encodeURIComponent(url)}`
  return (
    <img
      src={src}
      width={180}
      height={180}
      alt="QR code to open Pocket Companion on your phone"
      style={{
        display: 'block',
        margin: '0 auto',
        borderRadius: '0.75rem',
        background: '#fff',
        border: '1px solid var(--border, #ddd)',
      }}
    />
  )
}

export function PhoneOnlyPage() {
  useDocumentTitle('Download Companion')
  const [params] = useSearchParams()
  const from = params.get('from') || ''
  const url = companionPublicUrl()
  const phone = isPhoneCompanionClient()

  if (phone) {
    const next = from && from.startsWith('/') ? from : '/login?mode=signup'
    return (
      <div className="page stack" style={{ maxWidth: '24rem', paddingTop: '3rem' }}>
        <WordMark to="/" />
        <h1>Download Companion</h1>
        <p className="muted">
          Add Companion to your Home Screen, then sign in to link Pocket and sync with Pocket Cloud.
        </p>
        <Link className="btn btn-primary btn-block" to={next}>
          Continue
        </Link>
        <Link className="btn btn-ghost btn-block" to="/login">
          Sign in
        </Link>
      </div>
    )
  }

  return (
    <div className="page stack" style={{ maxWidth: '26rem', paddingTop: '3rem' }}>
      <WordMark to="/" />
      <h1>Companion is phone-only</h1>
      <p className="muted">
        Pocket Companion is a phone app — not a desktop website. Scan the QR code or open this page on
        your phone to download Companion.
      </p>
      <div className="panel stack" style={{ alignItems: 'center', textAlign: 'center' }}>
        <QrImg url={url} />
        <p className="muted" style={{ fontSize: '0.875rem', wordBreak: 'break-all' }}>
          {url}
        </p>
      </div>
      <p className="muted" style={{ textAlign: 'center', fontSize: '0.875rem' }}>
        Phone only — scan QR / open this page on your phone
      </p>
      <div className="actions-row" style={{ justifyContent: 'center' }}>
        <Link className="btn btn-ghost" to="/">
          Back to Pocket
        </Link>
        <Link className="btn btn-secondary" to="/login?return_to=%2Fadmin%2F">
          Owner sign in
        </Link>
      </div>
    </div>
  )
}
