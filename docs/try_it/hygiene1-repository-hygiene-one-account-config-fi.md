---
id: hygiene1
row: | hygiene1 | Account address and key in one config file; no built programs in git | Nothing to open: build or release as usual, optional key rotation in Supabase | Verified on Linux (tests, configure); no release or Windows run |
---
## hygiene1. Repository hygiene: one account config file, no built programs in git

What changes for you:

1. **Nothing is required in GitHub.** The Supabase address and publishable key are now in one plain file, `config/public-account.json` (explained in `config/README.md`). Every build reads it. The GitHub repository variables `OKAY_DEFAULT_ACCOUNT_URL` / `OKAY_DEFAULT_ACCOUNT_KEY` are an optional override; leave them unset.
2. **Optional: rotate the key.** In the Supabase dashboard (Project Settings > API) create a new publishable key, edit `key` in `config/public-account.json`, commit, then disable the old key. Keep the row-level security policies correct: they are what protects the data.
3. Try it: run a release or "Test build" as usual; sign in works as before. The build log shows "Account server baked in: yes (from config/public-account.json)" (the key is never printed). To see the empty case, build locally with the file removed: the launcher prints `[account] No account server is set in this build` and starts without online features.
4. `dist/` no longer holds programs. Use the installer, the zip or the launcher from a release, or build with `scripts/build-windows-dist.sh` (output `build-win-dist/`).
5. New check: a branch that adds an `.exe`/`.dll`/`.so`/archive or a big binary file fails "No built programs or big binary files in git" (`python3 scripts/branch_lint.py binaries`).

**Verified here:** the tests, on Linux. **Unverified:** no release, dev build or Pages run happened (workflows were read and checked as YAML only); nothing was run on Windows.
