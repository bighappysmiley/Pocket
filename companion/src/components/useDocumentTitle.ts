import { useEffect } from 'react'

export function useDocumentTitle(title: string) {
  useEffect(() => {
    document.title = title ? `${title} · Pocket Version 1` : 'Pocket Version 1'
  }, [title])
}
