import { useEffect, useState, type ReactNode } from 'react'
import { Link, Navigate } from 'react-router-dom'
import { useAuth } from '../lib/auth'
import { companionPublicUrl, isPhoneCompanionClient } from '../lib/utils'
import { useDocumentTitle } from '../components/useDocumentTitle'
import './landing.css'

const FIRMWARE_RELEASE =
  'https://github.com/bighappysmiley/Pocket/releases/tag/firmware-latest'
const REPO_URL = 'https://github.com/bighappysmiley/Pocket'
const DOCS_URL = `${REPO_URL}#readme`

const FEATURES = [
  {
    key: 'notes',
    title: 'Notes',
    body: 'Dictate on the device, edit on your phone. Pocket Cloud keeps Notes in sync when you want them everywhere.',
  },
  {
    key: 'lists',
    title: 'Lists',
    body: 'Shopping, packing, and everyday checklists that stay calm on e-ink and handy in Companion.',
  },
  {
    key: 'music',
    title: 'Music',
    body: 'Load tracks from Companion, play on Pocket. Simple transport — no feed, no noise.',
  },
  {
    key: 'reading',
    title: 'Reading',
    body: 'A quiet reading surface. Sync books from Companion and pick up where you left off.',
  },
] as const

function DeviceHero() {
  return (
    <div className="landing-device" aria-hidden="true">
      <div className="landing-device__glow" />
      <svg
        className="landing-device__svg"
        viewBox="0 0 320 520"
        role="img"
        focusable="false"
      >
        <defs>
          <linearGradient id="caseGrad" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0%" stopColor="#2a332e" />
            <stop offset="100%" stopColor="#1a1f1c" />
          </linearGradient>
          <linearGradient id="screenGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#f4f2ec" />
            <stop offset="100%" stopColor="#e8e4da" />
          </linearGradient>
        </defs>
        <rect x="36" y="12" width="248" height="496" rx="36" fill="url(#caseGrad)" />
        <rect x="52" y="44" width="216" height="408" rx="8" fill="url(#screenGrad)" />
        <text
          x="160"
          y="88"
          textAnchor="middle"
          fill="#1a1f1c"
          fontFamily="Fraunces, Georgia, serif"
          fontSize="22"
          fontWeight="600"
        >
          Pocket
        </text>
        <text
          x="160"
          y="118"
          textAnchor="middle"
          fill="#5c6560"
          fontFamily="Outfit, sans-serif"
          fontSize="13"
        >
          Classic
        </text>
        <g fill="none" stroke="#1a1f1c" strokeWidth="1.5">
          <rect x="78" y="150" width="52" height="52" rx="10" />
          <rect x="134" y="150" width="52" height="52" rx="10" />
          <rect x="190" y="150" width="52" height="52" rx="10" />
          <rect x="78" y="214" width="52" height="52" rx="10" />
          <rect x="134" y="214" width="52" height="52" rx="10" />
          <rect x="190" y="214" width="52" height="52" rx="10" />
        </g>
        <g fill="#1a1f1c" fontFamily="Outfit, sans-serif" fontSize="9" textAnchor="middle">
          <text x="104" y="220">Notes</text>
          <text x="160" y="220">Lists</text>
          <text x="216" y="220">Music</text>
          <text x="104" y="284">Read</text>
          <text x="160" y="284">Weather</text>
          <text x="216" y="284">Settings</text>
        </g>
        <rect x="78" y="320" width="164" height="88" rx="12" fill="#1a1f1c" opacity="0.06" />
        <text
          x="160"
          y="358"
          textAnchor="middle"
          fill="#1a1f1c"
          fontFamily="Fraunces, Georgia, serif"
          fontSize="28"
          fontWeight="500"
        >
          9:41
        </text>
        <text
          x="160"
          y="386"
          textAnchor="middle"
          fill="#5c6560"
          fontFamily="Outfit, sans-serif"
          fontSize="11"
        >
          Monday
        </text>
        <circle cx="160" cy="478" r="14" fill="#3d4a43" />
        <circle cx="278" cy="200" r="8" fill="#3d4a43" />
        <circle cx="278" cy="240" r="8" fill="#3d4a43" />
      </svg>
    </div>
  )
}

function FeatureVisual({ active }: { active: (typeof FEATURES)[number]['key'] }) {
  const panels: Record<(typeof FEATURES)[number]['key'], { label: string; lines: string[] }> = {
    notes: {
      label: 'Notes',
      lines: ['Grocery ideas', 'Call Sam after lunch', 'Weekend packing'],
    },
    lists: {
      label: 'Lists',
      lines: ['☐ Oat milk', '☐ Batteries', '☑ Boarding pass'],
    },
    music: {
      label: 'Music',
      lines: ['Now playing', 'Quiet mornings · Vol. 2', '▸  2:14  /  4:02'],
    },
    reading: {
      label: 'Reading',
      lines: ['Chapter 12', 'The page stays still.', 'Sync from Companion'],
    },
  }
  const panel = panels[active]
  return (
    <div className="landing-feature-visual" key={active}>
      <div className="landing-feature-visual__chrome">
        <span />
        <span />
        <span />
      </div>
      <p className="landing-feature-visual__label">{panel.label}</p>
      <ul>
        {panel.lines.map((line) => (
          <li key={line}>{line}</li>
        ))}
      </ul>
    </div>
  )
}

function DownloadCompanionCta({
  className,
  children,
}: {
  className?: string
  children?: ReactNode
}) {
  const phone = isPhoneCompanionClient()
  const label = children ?? 'Download Companion'
  if (phone) {
    return (
      <Link className={className} to="/login?mode=signup">
        {label}
        <span aria-hidden="true">→</span>
      </Link>
    )
  }
  return (
    <a className={className} href="#download">
      {label}
      <span aria-hidden="true">→</span>
    </a>
  )
}

function DownloadPanel() {
  const phone = isPhoneCompanionClient()
  const url = companionPublicUrl()
  const qr = `https://api.qrserver.com/v1/create-qr-code/?size=160x160&margin=8&data=${encodeURIComponent(url)}`

  if (phone) {
    return (
      <div className="landing-download">
        <h2 className="landing-section__title">Download Companion</h2>
        <p className="landing-section__lede">
          Install Companion on this phone — add to Home Screen, then sign in to link Pocket and use
          Pocket Cloud.
        </p>
        <div className="landing-hero__ctas">
          <Link className="landing-btn landing-btn--solid landing-btn--lg" to="/login?mode=signup">
            Download Companion
            <span aria-hidden="true">→</span>
          </Link>
          <Link className="landing-btn landing-btn--soft landing-btn--lg" to="/login">
            Sign in
          </Link>
        </div>
      </div>
    )
  }

  return (
    <div className="landing-download">
      <h2 className="landing-section__title">Download Companion</h2>
      <p className="landing-section__lede">
        Companion is phone-only — not a desktop web app. Scan the QR or open this page on your phone
        to install.
      </p>
      <div className="landing-download__grid">
        <img
          className="landing-download__qr"
          src={qr}
          width={160}
          height={160}
          alt="QR code to open Pocket on your phone"
        />
        <div className="landing-download__copy">
          <p className="landing-download__hint">Phone only — scan QR / open this page on your phone</p>
          <p className="landing-download__url">{url}</p>
          <Link className="landing-btn landing-btn--soft" to="/get-companion">
            How to install
          </Link>
        </div>
      </div>
    </div>
  )
}

export function LandingPage() {
  useDocumentTitle('Pocket')
  const { isAuthenticated, loading } = useAuth()
  const [feature, setFeature] = useState<(typeof FEATURES)[number]['key']>('notes')
  const [menuOpen, setMenuOpen] = useState(false)
  const phone = isPhoneCompanionClient()

  useEffect(() => {
    const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches
    if (reduced) return
    const id = window.setInterval(() => {
      setFeature((prev) => {
        const idx = FEATURES.findIndex((f) => f.key === prev)
        return FEATURES[(idx + 1) % FEATURES.length].key
      })
    }, 5000)
    return () => window.clearInterval(id)
  }, [])

  if (!loading && isAuthenticated && phone) {
    return <Navigate to="/home" replace />
  }

  const active = FEATURES.find((f) => f.key === feature) ?? FEATURES[0]

  return (
    <div className="landing">
      <nav className="landing-nav" aria-label="Marketing">
        <a className="landing-brand" href="#top">
          <span className="landing-brand__mark" aria-hidden="true" />
          <span>Pocket</span>
        </a>
        <div className="landing-nav__links">
          <a href="#shop">Shop</a>
          <a href="#features">Features</a>
          <a href="#download">Companion</a>
          <a href="#platform">Platform</a>
          <Link to="/developer">Developers</Link>
        </div>
        <div className="landing-nav__actions">
          <Link className="landing-btn landing-btn--ghost" to="/login">
            Sign in
          </Link>
          <DownloadCompanionCta className="landing-btn landing-btn--solid landing-nav__cta" />
          <button
            type="button"
            className="landing-nav__menu"
            aria-expanded={menuOpen}
            aria-controls="landing-mobile-menu"
            onClick={() => setMenuOpen((v) => !v)}
          >
            <span className="sr-only">Menu</span>
            <span aria-hidden="true">{menuOpen ? '✕' : '☰'}</span>
          </button>
        </div>
      </nav>

      {menuOpen ? (
        <div id="landing-mobile-menu" className="landing-mobile">
          <a href="#shop" onClick={() => setMenuOpen(false)}>
            Shop
          </a>
          <a href="#features" onClick={() => setMenuOpen(false)}>
            Features
          </a>
          <a href="#download" onClick={() => setMenuOpen(false)}>
            Companion
          </a>
          <a href="#platform" onClick={() => setMenuOpen(false)}>
            Platform
          </a>
          <Link to="/developer" onClick={() => setMenuOpen(false)}>
            Developers
          </Link>
          <Link to="/login" onClick={() => setMenuOpen(false)}>
            Sign in
          </Link>
          {phone ? (
            <Link
              className="landing-btn landing-btn--solid"
              to="/login?mode=signup"
              onClick={() => setMenuOpen(false)}
            >
              Download Companion
            </Link>
          ) : (
            <a
              className="landing-btn landing-btn--solid"
              href="#download"
              onClick={() => setMenuOpen(false)}
            >
              Download Companion
            </a>
          )}
        </div>
      ) : null}

      <main id="top">
        <header className="landing-hero">
          <h1 className="landing-hero__title">
            <span className="landing-hero__word" style={{ ['--i' as string]: 0 }}>
              welcome
            </span>{' '}
            <span className="landing-hero__word" style={{ ['--i' as string]: 1 }}>
              to
            </span>
            <br className="landing-hero__break" />
            <span className="landing-hero__word" style={{ ['--i' as string]: 2 }}>
              a
            </span>{' '}
            <span className="landing-hero__word landing-hero__word--accent" style={{ ['--i' as string]: 3 }}>
              quieter
            </span>{' '}
            <span className="landing-hero__word" style={{ ['--i' as string]: 4 }}>
              pocket
            </span>
          </h1>
          <p className="landing-hero__lede">
            Pocket devices for notes, lists, music, and reading — paired with Companion on your phone
            when you want the cloud.
          </p>
          <div className="landing-hero__ctas">
            <DownloadCompanionCta className="landing-btn landing-btn--solid landing-btn--lg" />
            <a className="landing-btn landing-btn--soft landing-btn--lg" href="#shop">
              Shop devices
            </a>
          </div>
          {!phone ? (
            <p className="landing-hero__phone-hint">
              Phone only — scan QR / open this page on your phone
            </p>
          ) : null}
          <div className="landing-hero__social">
            <a href="#shop">Shop</a>
            <a href={REPO_URL} target="_blank" rel="noreferrer" aria-label="Pocket on GitHub">
              Source
            </a>
            <Link to="/developer">Developers</Link>
          </div>
          <DeviceHero />
        </header>

        <section id="shop" className="landing-section landing-shop">
          <h2 className="landing-section__title">Shop</h2>
          <p className="landing-section__lede">
            Buy a Pocket device, then download Companion on your phone. The product line starts with
            Classic — we only list what ships.
          </p>
          <div className="landing-shop__card">
            <div className="landing-shop__meta">
              <p className="landing-shop__eyebrow">Device</p>
              <h3>Pocket Classic</h3>
              <p>
                Calm e-ink for Notes, Lists, Music, and Reading. Works offline; optional Pocket Cloud
                sync via Companion.
              </p>
            </div>
            <div className="landing-shop__actions">
              <a className="landing-btn landing-btn--solid" href="#shop">
                Shop Classic
                <span aria-hidden="true">→</span>
              </a>
              <p className="landing-shop__note">
                Storefront link coming — this section is the real shop CTA until checkout goes live.
              </p>
            </div>
          </div>
        </section>

        <section id="features" className="landing-section landing-features">
          <h2 className="landing-section__title">
            <span>Everyday</span> <span>tools,</span> <span>quietly</span>
          </h2>
          <p className="landing-section__lede">
            Pocket keeps the useful apps close — without feeds, badges, or glass chrome. Companion
            handles linking, sync, and the bits that belong on a phone.
          </p>
          <div className="landing-features__grid">
            <div className="landing-features__list">
              <div className="landing-features__tabs" role="tablist" aria-label="Features">
                {FEATURES.map((f) => (
                  <button
                    key={f.key}
                    type="button"
                    role="tab"
                    aria-selected={feature === f.key}
                    className={feature === f.key ? 'is-active' : undefined}
                    onClick={() => setFeature(f.key)}
                  >
                    {f.title}
                  </button>
                ))}
              </div>
              <div className="landing-features__desktop" role="tablist" aria-label="Features">
                {FEATURES.map((f) => (
                  <button
                    key={f.key}
                    type="button"
                    role="tab"
                    aria-selected={feature === f.key}
                    className={`landing-feature${feature === f.key ? ' is-active' : ''}`}
                    onClick={() => setFeature(f.key)}
                  >
                    <span className="landing-feature__title">{f.title}</span>
                    <span className="landing-feature__body">{f.body}</span>
                  </button>
                ))}
              </div>
              <p className="landing-features__mobile-body">{active.body}</p>
            </div>
            <div className="landing-features__visual">
              <FeatureVisual active={feature} />
            </div>
          </div>
        </section>

        <section id="download" className="landing-section landing-cloud">
          <DownloadPanel />
        </section>

        <section className="landing-section landing-cloud">
          <h2 className="landing-section__title">Pocket Cloud</h2>
          <p className="landing-section__lede">
            Optional sync for Notes, Lists, Music, Reading, backup, and connectors —{' '}
            <strong>$3.99/mo</strong> with a 7-day trial. The device works offline without it.
          </p>
          <div className="landing-hero__ctas">
            <DownloadCompanionCta className="landing-btn landing-btn--solid landing-btn--lg" />
            <a className="landing-btn landing-btn--soft landing-btn--lg" href="#shop">
              Shop Classic
            </a>
          </div>
        </section>

        <section id="platform" className="landing-section landing-values">
          <h2 className="landing-section__title">
            <span>Open</span> <span>source,</span> <span>closed</span> <span>platform</span>
          </h2>
          <p className="landing-section__lede">
            Source is available to read. The device is for buyers to use as shipped — not a casual
            alternate-OS playground. Tweaks go through an approved developer path.
          </p>
          <ul className="landing-values__list">
            <li>
              <Link to="/developer" className="landing-btn landing-btn--soft">
                Developer console
              </Link>
            </li>
            <li className="landing-values__chip">
              <span aria-hidden="true">✓</span> Buy &amp; use the product
            </li>
            <li className="landing-values__chip">
              <span aria-hidden="true">✓</span> Approved extensions
            </li>
            <li className="landing-values__chip">
              <span aria-hidden="true">✓</span> Official updates &amp; recovery
            </li>
          </ul>
        </section>
      </main>

      <footer className="landing-footer">
        <div className="landing-footer__inner">
          <div className="landing-footer__brand">
            <p className="landing-footer__name">Pocket</p>
            <p>
              Devices for a quieter pocket — Companion on your phone, optional Pocket Cloud, source
              available with a guided platform.
            </p>
          </div>
          <DownloadCompanionCta className="landing-btn landing-btn--paper" />
          <div className="landing-footer__cols">
            <div>
              <h3>Get started</h3>
              <ul>
                <li>
                  <a href="#download">Download Companion</a>
                </li>
                <li>
                  <a href="#shop">Shop Classic</a>
                </li>
                <li>
                  <Link to="/developer">Developer console</Link>
                </li>
                <li>
                  <a href={DOCS_URL} target="_blank" rel="noreferrer">
                    Documentation
                  </a>
                </li>
              </ul>
            </div>
            <div>
              <h3>Owners</h3>
              <ul>
                <li>
                  <Link to="/admin/">Cloud Admin</Link>
                </li>
                <li>
                  <a href={FIRMWARE_RELEASE} target="_blank" rel="noreferrer">
                    Official firmware / recovery
                  </a>
                </li>
                <li>
                  <a href={REPO_URL} target="_blank" rel="noreferrer">
                    Source on GitHub
                  </a>
                </li>
              </ul>
            </div>
          </div>
          <p className="landing-footer__copy">Pocket · Companion phone app · GitHub Pages</p>
          <div className="landing-footer__rings" aria-hidden="true" />
        </div>
      </footer>
    </div>
  )
}
