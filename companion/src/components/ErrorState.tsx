interface ErrorStateProps {
  message?: string
  onRetry?: () => void
}

export function ErrorState({
  message = "You're offline or the server is unreachable.",
  onRetry,
}: ErrorStateProps) {
  return (
    <div className="error-box" role="alert">
      <p>{message}</p>
      {onRetry ? (
        <button type="button" className="btn btn-secondary" onClick={onRetry}>
          Try again
        </button>
      ) : null}
    </div>
  )
}
