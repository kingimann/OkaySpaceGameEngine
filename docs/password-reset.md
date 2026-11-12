# Password reset page (Supabase)

"Forgot password?" in the launcher asks Supabase to email a reset link. That link opens a web page where the player
types a new password. This repo provides that page; **nothing is hosted or changed for you.**

- Page: `supabase/auth-pages/reset-password/index.html` (one file, plain HTML and JavaScript, no libraries, no CDN).
- It reads the recovery token from the URL fragment (`#access_token=...`), removes it from the address bar, asks for a new
  password twice (8 to 72 characters, Show/Hide button) and sends `PUT <project>/auth/v1/user` with the token.
  It shows a success, a mismatch/too-short, an expired-link or a server error message. It never logs, stores
  (no cookies, localStorage or sessionStorage) or forwards the token or password, and sends no referrer.

## Owner steps (once)

1. **Fill in two values** near the top of the script in `index.html`:
   `OKAY_RESET_CONFIG = { supabaseUrl: "https://<ref>.supabase.co", anonKey: "<publishable key>" }`.
   Both are public values (the same ones the launcher ships). Never use a service-role key. Until they are filled in,
   the page shows "This page is not set up yet" and does nothing else.
2. **Host the file over HTTPS** (this repository publishes it for you: `.github/workflows/pages.yml` puts only `supabase/auth-pages` on GitHub Pages at `https://<owner>.github.io/<repo>/reset-password/` once Settings > Pages > Source is set to "GitHub Actions"). Any static host also works: at any static host (GitHub Pages, Netlify, Cloudflare Pages, a bucket with website
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
- URL not set: it still sends the email (Supabase then uses the Site URL) but says plainly that no reset page is set
  up for this build, in red, and points here. It does not claim success.

## Tests

- `tests/scripts/test_password_reset_page.js` (ctest `password_reset_page`, needs `node`): token parsing,
  password rules, config check, request body/headers, error messages, no logging/storage calls.
- `tests/test_account.cpp`: the `/auth/v1/recover?redirect_to=` URL the launcher builds.
- `tests/scripts/test_password_reset_page_e2e.js` (by hand: needs Playwright and Chromium): runs the page against a
  local fake Supabase and screenshots form, mismatch, server error, success, expired link and missing token.
  Run: `node tests/scripts/test_password_reset_page_e2e.js <screenshot-dir>`.

Not verified against a real Supabase project (no live project was touched).
