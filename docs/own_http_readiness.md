# Own HTTP client: readiness for replacing curl

This page answers one question: what must `okay::ownhttp` do before the default can flip from curl to our own client, what does it do today, and what is left. The client itself is described in `docs/own_http_client.md`; the removal rules are in `docs/own_code_roadmap.md`. Nothing here flips anything: `OKAY_OWN_HTTP` is still OFF by default, every curl path is still in the code, and Windows still uses WinHTTP.

Status words: **Done** (code and a test that runs), **Partial** (works for the callers we have, with a stated limit), **Missing** (not written). "Proof" is the file that shows it.

## 1. Where the programs use curl or HTTP

| # | Place | What it does | Own client path today |
|---|---|---|---|
| 1 | `engine/include/okay/Platform/Account/AccountService.hpp` `HttpRequest` (all account, backend, ticket, upload calls; 12 call sites) | GET/POST/PATCH/DELETE with JSON or a raw file body, `apikey` and `Authorization` headers, any status is an answer (the body carries `{"error"}`), a per-call timeout for the join-time ticket | `okay::http::Request` first when `NativeAvailable()`, then the curl fallback. Since #850 the per-call timeout reaches the own client (it did not before); on Windows it is NOT applied (WinHTTP keeps its session timeouts). A POST that may have reached the server is never re-sent through curl (`ShouldFallBackToCurl`, tested end to end) |
| 2 | `engine/include/okay/Platform/PlayFab/PlayFab.hpp` (one POST) | JSON POST to `*.playfabapi.com` | curl only (not wired to `NativeHttp`) |
| 3 | `launcher/main.cpp` `Download` (line ~329) | GET to a file with no-cache headers, redirects, `-f` (HTTP errors fail), then PowerShell | `okay::http::Request` first, then curl, then PowerShell |
| 4 | `launcher/main.cpp` (~452, ~697) `HeadSize` and a second head probe | `curl -sIL`, dumps headers to a file, reads the last `Content-Length` | curl only |
| 5 | `launcher/main.cpp` (~496) the cancellable game download | `curl -L -f --max-filesize`, the file grows, the UI polls and kills the child on cancel | curl only |
| 6 | `launcher/main.cpp` (~2971) link preview | `curl -L -f -m 8 --max-filesize 3000000 -r 0-262143` (first 256 KB only, Range), spoofed browser agent | curl only |
| 7 | `launcher/main.cpp` (~16562) `OfficialFetchFile` | download with `--retry 2`, cancel, progress by file size | curl only |
| 8 | `launcher/VersionsNet.cpp` `DownloadFile` (Versions page) | `-L -f`, 5 redirects, https-only redirects, speed limit, max size, cancel, progress, disk full | **New:** own client first (opt-in), curl as the fallback. Proof: `tests/test_launcher_download_own.cpp` |
| 9 | `editor/main.cpp` `Download` (~1944, update download) | `curl -L -f` with no-cache headers, then PowerShell | curl only (not touched: the owner asked for minimal `main.cpp` edits) |
| 10 | `scripts/*.sh`, `.github/workflows/*.yml`, `deploy/voice-server/install.sh` | CI and release tooling: downloads, Supabase publish (`POST`, `DELETE`, `--retry-all-errors`, `-w '%{http_code}'`, `Authorization`), `versions.json` checks | Not shipped to users. Stays curl (build tooling, not a runtime dependency) |

`popen`, `system` and `RunCmd` are only the shells that start curl in rows 3 to 9; no other HTTP client or library is in the tree (`grep` of `launcher/`, `editor/`, `engine/`, `shared/`, `tools/`, `scripts/`).

## 2. What those callers need against what the client does

| Need | Used by rows | Status | Proof |
|---|---|---|---|
| GET, POST, PATCH, DELETE, HEAD, any token method | 1, 3, 4, 8 | Done | `test_own_http.cpp` (`TestBuild`), `test_own_http_net.cpp` (`TestPlain`) |
| Caller headers (`apikey`, `Authorization`, `Content-Type`, `Cache-Control`, `Prefer`) | 1, 3, 9 | Done | `TestPlain` (`/hdrs`) |
| Body from a string or a file | 1 | Done (whole file read into memory; no streaming upload) | `TestNativeHttp` (POST body from a file) |
| Any HTTP status is an answer with its body | 1 | Done | `TestNativeHttp` (404 with body) |
| Redirects (301/302/303/307/308, relative, loop cap, https to http refused, credentials dropped across origins) | 3, 5, 8, 9 | Done | `test_own_http.cpp` (`TestFetch`), `TestPlain`, `TestHostile` |
| Redirect cap of 5 and "https only" redirects like the curl flags | 8 | Cap: Done. `DownloadFile` sets 5, and a server that redirects forever gets exactly 6 requests, then the call fails. Refusing https to http: Done in the pure layer (the client always refuses it). Gap: no TLS server in the launcher test, so the launcher-level https to http case is not run end to end | Cap: `test_launcher_download_own.cpp` (`/loop`, 6 hits) and `TestReviewFixes` in `test_own_http_net.cpp`, plus `TestFetch` (loop cap) in `test_own_http.cpp`. Downgrade: `TestFetch` (https to http, `DowngradeRefused`, one request only). In `VersionsNet.cpp` a refused downgrade is not an answer, so the download then falls back to curl, whose `--proto-redir =https` flag is the final guard |
| Chunked responses | all | Done | `TestParse`, `TestPlain` (`/chunked`) |
| gzip and deflate responses | none today (we ask for identity) | **Done in #850, opt-in** (peak memory about 2 times the cap: the encoded copy plus the decoded body) (`NetOptions::acceptGzip`, in memory, capped, own inflate, CRC and Adler checked, bomb capped) | `test_own_http.cpp` (`TestInflate`, `TestContentCoding`), `TestDownloadFeatures` |
| brotli, zstd | none | Missing, not needed (we never ask for them) | none |
| Streaming to a file | 3, 5, 7, 8, 9 | Done | `TestNativeHttp`, `test_launcher_download_own.cpp` |
| Resume and ranges (`Range: bytes=N-`, 206, 416) | 6 (range), future resume | **Done in #850, tightened in the review fixes.** A 206 must carry a valid `Content-Range` that matches its `Content-Length`; `bytes=N-` and `bytes=N-M` must start at N (N-M must also end at or before M); `bytes=-N` needs a known size, an end at the last byte and a length of at most N; multipart/byteranges and 206 answers to multi-range or malformed Range headers are refused; an unasked 206 is refused; a 200 means start again; `RequestEx` appends to the file and leaves it alone on 404 and 416 | `TestRange`, `TestRangeStrict`, `TestReviewFixes`, `TestDownloadFeatures` |
| Progress callback and cancel | 5, 7, 8 | **Done in #850.** `FetchHooks::onProgress`, `RequestOptions::progress`, `RequestOptions::cancel` (polled every 200 ms, also while the server is silent, while connecting and while resolving) | `TestRange`, `TestDownloadFeatures`, `test_launcher_download_own.cpp` |
| Timeouts: connect, silence, total | 1, 5, 6, 8 | Done (connect 8 s per address, silence 30 s, total 15 min; per call through `timeoutMs` or `RequestOptions`) | `TestPlain`, `TestHostile`, `TestDownloadFeatures` |
| Name resolution timeout | all | **Done in #850** (helper thread, `resolveMs`, cancel). A resolver that never answers leaves its thread behind, but the caller is free | `TestDownloadFeatures` (only the "name does not exist" and "cancelled" cases; a hung resolver is not simulated) |
| Max download size (`--max-filesize`) | 5, 6, 8 | Done (`limits.maxBodyBytes`, `RequestOptions::maxBytes`; checked on `Content-Length` and while reading) | `TestPlain` (caps), `test_launcher_download_own.cpp` |
| Proxy from the environment (`HTTPS_PROXY`, `HTTP_PROXY`, `NO_PROXY`) with CONNECT | all | Done | `TestProxy`, `TestNoProxyList` |
| Proxy credentials, SOCKS, `https://` proxies, PAC, `ALL_PROXY` | corporate networks | Missing. A setting we cannot honour is an error (then curl runs), never a direct connection | `TestProxy` |
| Cookies | none (the APIs use bearer tokens) | Missing on main. Open PR #852 adds them; not merged | none |
| Multipart upload (`-F`) | none | Missing, not needed (uploads are raw file bodies) | none |
| HTTP/2 | none (WinHTTP asks for it on Windows; the servers we use answer HTTP/1.1) | Missing, not needed for correctness | none |
| Keep-alive and connection reuse | launcher startup (many small calls) | Missing on main (open PR #852 adds it; not merged). One TCP and TLS handshake per request and per redirect. Slower than WinHTTP's pool; correct | none |
| TLS 1.2+, SNI, chain and host name checks, no way to turn them off | all https | Done (Linux, system OpenSSL loaded at run time; not our own TLS) | `TestTls`, `TestNoVerifyBypass` |
| CA store: system default, `SSL_CERT_FILE`, `CURL_CA_BUNDLE`, `SSL_CERT_DIR` | all https | Done on Linux | `TestTls` |
| Windows certificate store | all https on Windows | **Missing, by decision.** Windows keeps WinHTTP, which uses the Windows store and proxy settings. An own client on Windows would need TLS (Schannel) and the store through `CertOpenSystemStore`, and that is TLS work next to the crypto rule (`docs/crypto.md`); it cannot be tested here (no Windows). | none |
| macOS TLS | Mac builds | Missing (macOS is parked) | none |
| Client certificates, pinning, OCSP | none | Missing, not needed | none |
| Windows | all | Not applicable: the own client is not compiled in on Windows | none |

## 3. What changed for readiness: which PR added what

- **PR #850 (merged; its title wrongly says "keep-alive, HEAD, conditional requests, cookies"; the title cannot be changed on the merged commit):** gzip and deflate (own inflate), Range and resume, progress, cancel, the name-lookup timeout, `RequestEx`, the per-call timeout on the own client, the launcher `DownloadFile` move, the `own-http` CI job. It does NOT contain keep-alive, HEAD requests as a feature, conditional requests or cookies.
- **Test gaps left open on main (checked when closing the "not covered" list):** keep-alive over TLS is untested because main has no keep-alive (PR #852) and no TLS server of our own (only `openssl s_server`, which closes after each answer); the PlayFab POST on a reused connection has no test because `PlayFab.hpp` on main still calls the system `curl` only (no own-client path, no `test_playfab_own.cpp`). The `AccountService::ApiTimed` stall case is now covered (see `docs/own_http_client.md`).
- **PR #852 (open, not merged):** keep-alive, HEAD, conditional requests, cookies and moving more call sites onto the client. Until it merges, every line on this page that says these are Missing is the state of main.
- **Review fixes (this branch `claude/ownhttp-review-fixes`):** Windows timeouts back to what they were, no curl re-send of a POST that may have been sent, strict Range checks, loopback tests for the redirect cap, and the CI and memory notes.

Details of #850:

- `OwnHttpInflate.hpp`: gzip and zlib decoder of our own (compression, not cryptography), wired behind `acceptGzip` and `Limits::allowGzip`; off unless asked for, so every existing test and caller behaves as before.
- Range handling in `Fetch`: strict `Content-Range` checks, `FetchResult::partial/rangeStart/rangeTotal`.
- `FetchHooks::onProgress`, `NetOptions::cancel`, `NetOptions::resolveMs`, a resolver on a helper thread.
- `okay::http::RequestEx` (progress, cancel, resume, size cap, timeouts, gzip) and a `timeoutMs` argument on `okay::http::Request`; the account layer passes its per-call timeout to it (before, only the curl fallback honoured it, so with the own client on, the join-time ticket call could wait up to 15 minutes). On Windows (WinHTTP) the argument is ignored; Windows keeps its session timeouts. A timeout on the own client no longer leads to a second send of a POST by curl (`Result::maybeSent`).
- `launcher/VersionsNet.cpp` `DownloadFile` tries the own client first (only when built with `OKAY_OWN_HTTP=ON` and run with `OKAY_OWN_HTTP=1`), then curl. The Windows timeouts are untouched (the review fixes removed a per-request WinHTTP timeout that #850 had added). Windows changes made by request (not run on Windows; syntax-checked with MinGW only): any WinHTTP failure after the send succeeded marks the call as maybe-sent (so a POST is not re-sent through curl), and a failed body read or a body shorter than `Content-Length` now fails the call instead of returning a cut body with status 200. The decision is `okay::http::ShouldFallBackToCurl` (tested for every method, and end to end through `AccountService` with a counting server and a fake `curl` on PATH).
- **Rule for PR #852 (pooled connections):** `FetchResult::requestSent` is set immediately before EVERY write of request bytes. On main there is one write site and the flag is set just before it. #852 skips `conn.Open` for a pooled (kept-alive) connection and retries on a stale one; its reused and retried paths must set the flag before their own write too, or a POST sent on a reused connection would look "not sent" and be re-sent by curl.
- Tests: after #850 `test_own_http` had 3,961 checks (was 147), `test_own_http_net` 181 (was 156), `test_launcher_download_own` 6 (new); the CI `own-http` job builds and runs the third. After the review fixes (20,000 fuzz iterations, ASan and UBSan, run here): 3,987, 193 and 7.

## 4. The flip criteria (`docs/own_code_roadmap.md`, "How a library is removed", step 3)

| Criterion | Met? | Detail |
|---|---|---|
| 1. A real-use corpus passes (real servers, not only our own scripts) | **Not met** | Everything runs against local servers we wrote and `openssl s_server`. One manual run through the development sandbox's real proxy to one https host. Not run against GitHub Releases, Supabase, PlayFab or a CDN. A real-server run should cover: the launcher's version index and an installer download from the release host, a Supabase REST call with a 4xx body, a redirect through a CDN, a download behind a corporate-style proxy. |
| 2. Platform tests pass on Windows and Linux | **Not met** | Linux only, one machine, OpenSSL 3.0.13. The `own-http` CI job ran green on GitHub for PR #850 head `1b455cfa`, including its ASan and UBSan step. There is no Windows run and no macOS run, and the client is not compiled on Windows at all. A Windows flip would first need the Schannel and certificate-store decision above. |
| 3. Fuzz under ASan/UBSan, soak for stateful code | **Partly met** | Parser mutation fuzz, 200,000 iterations clean (earlier change). The new inflate has its own checks (every truncation and every single bit flip of a gzip and zlib vector, a zip bomb, 3,000 random mutations per run) and ran clean under ASan/UBSan here with 20,000 parser iterations. It has not had a long fuzz run (the 200,000 run was not repeated) and there is no soak (hours of repeated downloads, thousands of connections, descriptor and thread leak checks). The resolver helper thread is the first stateful piece and has had no soak. |
| 4. No known bugs in our replacement | **Not met in the sense that matters** | No known crash or wrong answer. Known gaps that a real user can hit: no connection reuse (slower startup than WinHTTP), no proxy credentials or SOCKS (those users fall back to curl), a hung resolver leaves a thread, TLS is only as good as the machine's OpenSSL. |
| 5. Owner approval, written in the PR | **Not met** | Not asked. |

Also open from `docs/own_http_client.md`: the parser is new code with no outside review (the reason for the default staying OFF), and the curl uses in rows 2, 4, 5, 6, 7 and 9 are not moved onto `RequestEx` yet.

## 5. How close the flip is (honest estimate)

For Linux builds, the client now covers what the launcher and account callers need, with gaps in speed (no keep-alive) and in odd proxy setups (which fall back to curl). The code is about 70 percent of the way for Linux. What is missing is not code but evidence: real servers, a CI run on GitHub, a soak run and an outside look at the parser, plus owner approval. It is not ready to flip and nothing should be flipped on this page's word.

For the whole product it is much further away: Windows is 0 percent for the own client (it needs a TLS answer first), and macOS is not started. Removing curl as a dependency also needs rows 2, 4, 5, 6, 7 and 9 moved onto the client (about a day of work each, mostly in `launcher/main.cpp` and `editor/main.cpp`), and Windows still needs PowerShell or curl as the fallback until then.

## 6. Next steps, in order

1. (Done for #850: it ran green on `1b455cfa`.) Keep `own-http` green on every change.
2. A real-server run (list in criterion 1), written down with dates.
3. Move rows 5, 7 (cancellable downloads) onto `RequestEx` the way row 8 was, then 9, 4, 6, 2. Keep curl as the fallback for one release each.
4. A soak script (repeat downloads and API calls for an hour, watch descriptors and threads) and a 200,000-iteration fuzz run of the new inflate.
5. Keep-alive for the API host (a small pool), after the above, because it adds state.
6. Decide Windows (keep WinHTTP, which is the platform's own client and not third-party code) and macOS.
7. Outside review of the parser, then owner approval, then flip, then delete.
