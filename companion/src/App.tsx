import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom'
import { AuthProvider } from './lib/auth'
import { AppLayout, RequireAuth, RequireEntitlement } from './components/Layout'
import { AccountPage } from './pages/AccountPage'
import { AuthCallbackPage } from './pages/AuthCallbackPage'
import { BackupPage } from './pages/BackupPage'
import { BillingCancelPage, BillingPage, BillingSuccessPage } from './pages/BillingPage'
import { ConnectorsPage } from './pages/ConnectorsPage'
import { DeviceDetailPage } from './pages/DeviceDetailPage'
import { DeviceSetupPage } from './pages/DeviceSetupPage'
import { DevicesPage } from './pages/DevicesPage'
import { HomePage } from './pages/HomePage'
import { ListDetailPage } from './pages/ListDetailPage'
import { ListsListPage } from './pages/ListsListPage'
import { LoginPage } from './pages/LoginPage'
import { NoteDetailPage } from './pages/NoteDetailPage'
import { NotesListPage } from './pages/NotesListPage'
import { LinkPage, PairRedirect, WifiSetupRedirect } from './pages/LinkPage'
import { PassesPage } from './pages/PassesPage'
import { MusicPage } from './pages/MusicPage'
import { UpgradePage } from './pages/UpgradePage'
import { AdminPage } from './pages/AdminPage'

const routerBasename = (() => {
  const base = import.meta.env.BASE_URL || '/'
  const trimmed = base.replace(/\/$/, '')
  return trimmed === '' ? undefined : trimmed
})()

export default function App() {
  return (
    <AuthProvider>
      <BrowserRouter basename={routerBasename}>
        <Routes>
          <Route element={<AppLayout bare />}>
            <Route path="/login" element={<LoginPage />} />
            <Route path="/auth/callback" element={<AuthCallbackPage />} />
            <Route path="/wifi-setup" element={<WifiSetupRedirect />} />
            <Route path="/link" element={<LinkPage />} />
          </Route>

          <Route element={<RequireAuth />}>
            <Route element={<AppLayout />}>
              <Route path="/" element={<HomePage />} />
              <Route path="/passes" element={<PassesPage />} />
              <Route path="/devices" element={<DevicesPage />} />
              <Route path="/devices/:id" element={<DeviceDetailPage />} />
              <Route path="/devices/:id/setup" element={<DeviceSetupPage />} />
              <Route path="/pair" element={<PairRedirect />} />
              <Route path="/link" element={<LinkPage />} />
              <Route path="/billing" element={<BillingPage />} />
              <Route path="/billing/success" element={<BillingSuccessPage />} />
              <Route path="/billing/cancel" element={<BillingCancelPage />} />
              <Route path="/upgrade" element={<UpgradePage />} />
              <Route path="/account" element={<AccountPage />} />
              <Route path="/admin" element={<AdminPage />} />
              <Route path="/admin/" element={<AdminPage />} />

              <Route element={<RequireEntitlement />}>
                <Route path="/notes" element={<NotesListPage />} />
                <Route path="/notes/:id" element={<NoteDetailPage />} />
                <Route path="/lists" element={<ListsListPage />} />
                <Route path="/lists/:id" element={<ListDetailPage />} />
                <Route path="/music" element={<MusicPage />} />
                <Route path="/connectors" element={<ConnectorsPage />} />
                <Route path="/backup" element={<BackupPage />} />
              </Route>
            </Route>
          </Route>

          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </AuthProvider>
  )
}
