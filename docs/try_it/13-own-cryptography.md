---
id: 13
group: switches
row: | 13 | Own crypto | none, on purpose | **Cannot and must not be tried** |
---
### 13. Own cryptography

Not switchable, by owner decision (2026-11-14, CLAUDE.md rule 18). libsodium is the only crypto in a release, the
environment variable `OKAY_OWN_CRYPTO` does nothing at run time, and the startup line must say `crypto=libsodium`.
Nothing to try.
