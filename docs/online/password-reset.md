# Password reset (Supabase)

> **What this is:** "Forgot password?" in the launcher: Supabase emails a reset code, typed into the launcher.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `online/`, paths and links checked; the text itself was not re-checked against the code).

**Since 2026-11-20 the reset happens inside the launcher, with a code, and needs no web page** (owner: "I don't want any one to
depend on GitHub"; the page used to be on GitHub Pages, and `.github/workflows/pages.yml` was removed). "Forgot password?" asks
Supabase to send the reset email, the launcher shows two boxes (the code from the email and a new password), and
`AccountService::ResetPasswordWithCode` sends `POST <project>/auth/v1/verify` (`type: recovery`, email, code) and then
`PUT <project>/auth/v1/user` with the short session it gets. The session is not kept.

Why not host the page on Supabase instead: Supabase serves HTML from Storage and from Edge Functions on the `*.supabase.co`
address as `text/plain` (a documented Supabase limit against hosting web pages there; only a paid custom domain lifts it), so
the page would show as source text. Unverified here: no upload was tried (no service key in this session).

**Owner step (dashboard, once):** Authentication > Email Templates > Reset Password: put the code in the email, for example
`Your OkaySpace reset code: {{ .Token }}` (the `{{ .ConfirmationURL }}` link can stay or go). Until this is done the email only
has a link, and the launcher's code boxes have no code to take. Nothing else changes; no key is involved.

The old page below still works if someone hosts it themselves and sets `OKAY_PASSWORD_RESET_URL`; nothing in this repository
publishes it any more.

## Owner steps (once)

1. **Fill in two values.** The page source holds none (`OKAY_RESET_CONFIG = { supabaseUrl: "", anonKey: "" }`). If you host the file yourself, edit
   the two values in your copy: `{ supabaseUrl: "https://<ref>.supabase.co", anonKey: "<publishable key>" }`.
   Both are public values (the same ones the launcher ships). Never use a service-role key. Until they are filled in,
   the page shows "This page is not set up yet" and does nothing else.
2. **Host the file over HTTPS** at any static host (Netlify, Cloudflare Pages, a bucket with website
   hosting, or your own web server), for example `https://example.com/reset-password/`. Serve it as-is; the page carries
   its own Content-Security-Policy.
3. **Supabase dashboard > Authentication > URL Configuration**:
   - add the exact page URL to **Redirect URLs** (the allow-list). If it is not on the list, Supabase ignores the
     `redirect_to` the launcher sends and falls back to the Site URL.
   - Site URL: leave it as is, or set it to the page URL if you want the page to be the fallback too.
4. **Tell the launcher the URL** (any one of):
   - environment variable `OKAY_PASSWORD_RESET_URL`,
   - a one-line file `password_reset_url.txt` next to the launcher exe,
   - build time: `cmake -DOKAY_DEFAULT_PASSWORD_RESET_URL=https://example.com/reset-password/ ...`.
5. Optional: Authentication > Email Templates > Reset Password keeps its default `{{ .ConfirmationURL }}` link;
   do not change it, the token in that link is what the page uses.

## What the launcher does

- URL set: "Forgot password?" sends the email with `redirect_to=<your page>` and says to open the link.
- URL not set (the normal case): it sends the email and shows the code and new-password boxes.

## Tests

- `tests/scripts/test_password_reset_page.js` (ctest `password_reset_page`, needs `node`): token parsing,
  password rules, config check, request body/headers, error messages, no logging/storage calls.
- `tests/test_account.cpp`: the `/auth/v1/recover?redirect_to=` URL the launcher builds, the reset-code check and the
  `/auth/v1/verify` body.
- `tests/scripts/test_password_reset_page_e2e.js` (by hand: needs Playwright and Chromium): runs the page against a
  local fake Supabase and screenshots form, mismatch, server error, success, expired link and missing token.
  Run: `node tests/scripts/test_password_reset_page_e2e.js <screenshot-dir>`.

Not verified against a real Supabase project (no live project was touched).
