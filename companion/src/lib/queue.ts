/** Minimal IndexedDB wrapper for offline mutation queue (Spec §11). */

const DB_NAME = 'pocket-companion'
const DB_VERSION = 1
const STORE = 'mutations'

export type MutationKind =
  | 'note.create'
  | 'note.update'
  | 'note.delete'
  | 'list.create'
  | 'list.update'
  | 'list.delete'

export interface QueuedMutation {
  id: string
  kind: MutationKind
  path: string
  method: string
  body: unknown
  created_at: string
}

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION)
    req.onupgradeneeded = () => {
      const db = req.result
      if (!db.objectStoreNames.contains(STORE)) {
        db.createObjectStore(STORE, { keyPath: 'id' })
      }
    }
    req.onsuccess = () => resolve(req.result)
    req.onerror = () => reject(req.error ?? new Error('IndexedDB open failed'))
  })
}

function txDone(tx: IDBTransaction): Promise<void> {
  return new Promise((resolve, reject) => {
    tx.oncomplete = () => resolve()
    tx.onerror = () => reject(tx.error ?? new Error('IndexedDB transaction failed'))
    tx.onabort = () => reject(tx.error ?? new Error('IndexedDB transaction aborted'))
  })
}

export async function enqueueMutation(mutation: Omit<QueuedMutation, 'id' | 'created_at'> & { id?: string }): Promise<QueuedMutation> {
  const entry: QueuedMutation = {
    id: mutation.id ?? crypto.randomUUID(),
    kind: mutation.kind,
    path: mutation.path,
    method: mutation.method,
    body: mutation.body,
    created_at: new Date().toISOString(),
  }
  const db = await openDb()
  try {
    const tx = db.transaction(STORE, 'readwrite')
    tx.objectStore(STORE).put(entry)
    await txDone(tx)
  } finally {
    db.close()
  }
  return entry
}

export async function listMutations(): Promise<QueuedMutation[]> {
  const db = await openDb()
  try {
    const tx = db.transaction(STORE, 'readonly')
    const store = tx.objectStore(STORE)
    const req = store.getAll()
    const rows = await new Promise<QueuedMutation[]>((resolve, reject) => {
      req.onsuccess = () => resolve((req.result as QueuedMutation[]) ?? [])
      req.onerror = () => reject(req.error ?? new Error('IndexedDB getAll failed'))
    })
    await txDone(tx)
    return rows.sort((a, b) => a.created_at.localeCompare(b.created_at))
  } finally {
    db.close()
  }
}

export async function removeMutation(id: string): Promise<void> {
  const db = await openDb()
  try {
    const tx = db.transaction(STORE, 'readwrite')
    tx.objectStore(STORE).delete(id)
    await txDone(tx)
  } finally {
    db.close()
  }
}

/** Spec §11 / §3.2: clear pending mutations on logout to avoid cross-account flush. */
export async function clearMutationQueue(): Promise<void> {
  const db = await openDb()
  try {
    const tx = db.transaction(STORE, 'readwrite')
    tx.objectStore(STORE).clear()
    await txDone(tx)
  } finally {
    db.close()
  }
}

export async function flushMutationQueue(
  send: (m: QueuedMutation) => Promise<void>,
): Promise<{ flushed: number; failed?: QueuedMutation }> {
  const all = await listMutations()
  let flushed = 0
  for (const m of all) {
    try {
      await send(m)
      await removeMutation(m.id)
      flushed += 1
    } catch {
      return { flushed, failed: m }
    }
  }
  return { flushed }
}
