#!/usr/bin/env node
/**
 * GitHub project Pages always returns HTTP 404 for unknown paths even when
 * 404.html is the SPA shell. Emit real directories so deep links return 200.
 */
import { copyFileSync, mkdirSync, existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const dist = join(root, 'dist')
const index = join(dist, 'index.html')

if (!existsSync(index)) {
  console.error('spa-pages-fallback: dist/index.html missing — run vite build first')
  process.exit(1)
}

copyFileSync(index, join(dist, '404.html'))

/** Routes that must open with HTTP 200 on a hard refresh / deep link. */
const spaDirs = ['admin', 'login', 'account', 'pair', 'devices', 'wifi-setup']

for (const dir of spaDirs) {
  const targetDir = join(dist, dir)
  mkdirSync(targetDir, { recursive: true })
  copyFileSync(index, join(targetDir, 'index.html'))
}

console.log(`spa-pages-fallback: 404.html + ${spaDirs.map((d) => d + '/').join(', ')}`)
