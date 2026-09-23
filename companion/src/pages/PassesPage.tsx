import { useDocumentTitle } from '../components/useDocumentTitle'

/** Spec §6.6 — Coming soon placeholder (Passes never sync in Cloud v1). */
export function PassesPage() {
  useDocumentTitle('Passes')

  return (
    <div className="page stack">
      <h1>Passes</h1>
      <div className="empty panel stack-sm">
        <h2>Coming soon</h2>
        <p className="muted">View passes from your Pocket on this phone in a free update.</p>
      </div>
    </div>
  )
}
