# config/public-account.json

The account server address (`url`) and the **publishable** (anon) key (`key`) of the OkaySpace Supabase project.
Every build reads this one file at configure time and bakes the values into the launcher, editor and player, and the
release scripts and the password-reset page read the project address from it.

* **The publishable key is public by design.** It ends up inside every program we ship, so anyone can read it. It is
  not a secret and must never be confused with the `service_role` key or a management token (those never go in the repo).
* **What actually protects the data is Supabase row-level security (RLS).** Keep the RLS policies on every table correct;
  see `docs/sql/` and `docs/online/supabase-backend.md`.
* **To rotate the key:** create a new publishable key in the Supabase dashboard (Project Settings > API), edit `key` in
  this file, commit, then disable the old key. The next build uses the new one.
* **Overrides:** `cmake -DOKAY_DEFAULT_ACCOUNT_URL=... -DOKAY_DEFAULT_ACCOUNT_KEY=...` or environment variables of the
  same names win over the file; so do the optional GitHub repository variables of the same names in CI.
* If the file is missing or empty the build still works, without online features.
* Keep it plain JSON with exactly the two string fields; no other file holds these values.
