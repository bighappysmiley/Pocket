/** Consumer-facing errors only — no maker/debug strings (Spec brand rules). */

export class AppError extends Error {
  constructor(
    public readonly status: number,
    public readonly code: string,
    message: string,
  ) {
    super(message);
    this.name = "AppError";
  }
}

export const Errors = {
  unauthorized: () => new AppError(401, "unauthorized", "Sign in to continue."),
  forbidden: () => new AppError(403, "forbidden", "You don't have access to that."),
  notEntitled: () =>
    new AppError(403, "not_entitled", "Pocket Cloud isn't active on this account."),
  notFound: (what = "That") => new AppError(404, "not_found", `${what} couldn't be found.`),
  badRequest: (message: string) => new AppError(400, "bad_request", message),
  conflict: (message: string) => new AppError(409, "conflict", message),
  pairExpired: () =>
    new AppError(
      410,
      "pair_expired",
      "This code has expired. Generate a new one on your Pocket.",
    ),
  pairInvalid: () => new AppError(404, "pair_invalid", "We couldn't find that code."),
  pairOtherAccount: () =>
    new AppError(
      409,
      "pair_other_account",
      "This Pocket is linked to another account. Unlink it there first.",
    ),
  rateLimited: () => new AppError(429, "rate_limited", "Please wait a moment and try again."),
};
