import { Link } from 'react-router-dom'
import { useDocumentTitle } from '../components/useDocumentTitle'
import { WordMark } from '../components/WordMark'

/**
 * Honest scaffold for the closed-platform developer path.
 * Submission / approval UI is not shipping yet — this page describes the model.
 */
export function DeveloperPage() {
  useDocumentTitle('Developer')

  return (
    <div className="page stack" style={{ maxWidth: '36rem', paddingTop: '2.5rem' }}>
      <WordMark to="/" />
      <div className="stack-sm">
        <h1>Developer console</h1>
        <p className="muted">
          Pocket is source-available with a <strong>closed platform</strong>: buyers use the device as
          shipped. Alternate OSes and casual full custom firmware are out of scope.
        </p>
      </div>

      <div className="panel stack">
        <h2 style={{ margin: 0, fontSize: '1.15rem' }}>Approved tweaks</h2>
        <p className="muted" style={{ margin: 0 }}>
          Extensions and device tweaks go through a developer submission path with review. Think
          guided surface area — not a free-for-all flash playground.
        </p>
        <ul className="muted" style={{ margin: 0, paddingLeft: '1.2rem' }}>
          <li>Describe the change and target (Companion, Cloud, or approved device surface)</li>
          <li>Submit for review from this console (UI coming)</li>
          <li>Ship only after approval onto the Pocket platform</li>
        </ul>
      </div>

      <div className="panel stack" role="status">
        <p style={{ margin: 0, fontWeight: 600 }}>Submission UI — coming</p>
        <p className="muted" style={{ margin: 0 }}>
          The approval workflow is not live yet. This page is the stub so product and docs stay honest
          about the closed platform. Firmware updates for owners remain via official{' '}
          <code>firmware-latest</code> / OTA — recovery, not open modding.
        </p>
      </div>

      <div className="actions-row">
        <Link className="btn btn-primary" to="/">
          Back to Pocket
        </Link>
        <a
          className="btn btn-ghost"
          href="https://github.com/bighappysmiley/Pocket"
          target="_blank"
          rel="noreferrer"
        >
          View source
        </a>
        <Link className="btn btn-ghost" to="/login?return_to=%2Fadmin%2F">
          Cloud Admin
        </Link>
      </div>
    </div>
  )
}
