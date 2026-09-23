import { useEffect, useState } from 'react'
import { dismissA2HS, isStandaloneDisplay, shouldShowA2HS } from '../lib/utils'

interface BeforeInstallPromptEvent extends Event {
  prompt: () => Promise<void>
  userChoice: Promise<{ outcome: 'accepted' | 'dismissed' }>
}

export function A2HSBanner() {
  const [visible, setVisible] = useState(false)
  const [deferred, setDeferred] = useState<BeforeInstallPromptEvent | null>(null)

  useEffect(() => {
    if (isStandaloneDisplay() || !shouldShowA2HS()) return

    const onBip = (e: Event) => {
      e.preventDefault()
      setDeferred(e as BeforeInstallPromptEvent)
      setVisible(true)
    }

    window.addEventListener('beforeinstallprompt', onBip)

    // iOS / browsers without BIP: still show coach mark
    const t = window.setTimeout(() => {
      if (!isStandaloneDisplay() && shouldShowA2HS()) setVisible(true)
    }, 1200)

    return () => {
      window.removeEventListener('beforeinstallprompt', onBip)
      window.clearTimeout(t)
    }
  }, [])

  if (!visible) return null

  const onDismiss = () => {
    dismissA2HS()
    setVisible(false)
  }

  const onAdd = async () => {
    if (deferred) {
      await deferred.prompt()
      await deferred.userChoice
      dismissA2HS()
      setVisible(false)
      return
    }
    // No native prompt — keep banner brief; user uses browser share/add
    onDismiss()
  }

  return (
    <div className="banner" role="region" aria-label="Add to Home Screen">
      <span className="banner-text">Add Pocket Cloud to your Home Screen</span>
      <div className="actions-row">
        <button type="button" className="btn btn-primary" onClick={() => void onAdd()}>
          Add
        </button>
        <button type="button" className="btn btn-ghost" onClick={onDismiss}>
          Not now
        </button>
      </div>
    </div>
  )
}
