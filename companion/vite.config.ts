import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// GitHub project Pages serve under /Pocket/ — set VITE_BASE=/Pocket/ in that deploy.
export default defineConfig({
  base: process.env.VITE_BASE || '/',
  plugins: [react()],
  server: {
    port: 5173,
    strictPort: true,
  },
  preview: {
    port: 5173,
    strictPort: true,
  },
})
