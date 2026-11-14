# Own HTTP client: readiness for replacing curl

This page answers one question: what must `okay::ownhttp` do before the default can flip from curl to our own client, what does it do today, and what is left. The client itself is described in `docs/own_http_client.md`; the removal rules are in `docs/own_code_roadmap.md`. Nothing here flips anything: `OKAY_OWN_HTTP` is still OFF by default, every curl path is still in the code (the own client always goes first and curl is the fallback), and Windows still uses WinHTTP. Every call site below that says "own client first" only does so when the program was built with `-DOKAY_OWN_HTTP=ON` and is run with `OKAY_OWN_HTTP=1`.

Status words: **Done** (code and a test that runs), **Partial** (works for the callers we have, with a stated limit), **Missing** (not written). "Proof" is the file that shows it.

## 1. Where the programs use curl or HTTP

| # | Place | What it does | Own client path today |
|---|---|---|---|
| 1 | `engine/include/okay/Platform/Account/AccountService.hpp` `HttpRequest` (all account, backend, ticket, upload calls; 12 call sites) | GET/POST/PATCH/DELETE with JSON or a raw file body, `apikey` and `Authorization` headers, any status is an answer (the body carries `{"error"}`), a per-call timeout for the join-time ticket | `okay::http::Request` first when `NativeAvailable()`, then the curl fallback. The per-call timeout reaches the own client. Sequential calls on one thread now reuse the connection (keep-alive) |
| 2 | `engine/include/okay/Platform/PlayFab/PlayFab.hpp` (one POST) | JSON POST to `*.playfabapi.com` | **Moved.** `okay::http::Request` first (Linux, opt-in; Windows keeps curl), any status is an answer, curl is the fallback. Proof: `tests/test_playfab_own.cpp` (loopback server, curl not on PATH) |
| 3 | `launcher/main.cpp` `Download` (line ~329) | GET to a file with no-cache headers, redirects, `-f` (HTTP errors fail), then PowerShell | `okay::http::Request` first, then curl, then PowerShell (already native first; now with keep-alive) |
| 4 | `launcher/main.cpp` (~452, ~697) `HeadSize` and a second head probe | `curl -sIL`, dumps headers to a file, reads the last `Content-Length` | **Moved.** `OwnHeadDump` (HEAD, follows redirects, same parser on the head text), curl is the fallback. Proof: `tests/test_native_fetch.cpp` (`TestHeadAndPrefix`) |
| 5 | `launcher/main.cpp` (~496) the cancellable game download | `curl -L -f --max-filesize`, the file grows, the UI polls and kills the child on cancel | **Moved.** `OwnDownload`: 2 GiB cap, live progress, cancel (also while the server is silent), `.part` rename, curl is the fallback. Proof: `TestDownload` |
| 6 | `launcher/main.cpp` (~2971) link preview | `curl -L -f -m 8 --max-filesize 3000000 -r 0-262143` (first 256 KB only, Range), spoofed browser agent | **Moved.** `OwnFetchPrefix`: Range request, 8 s total, 3 MB cap, 200 or 206, 404 or too big means no preview, curl is the fallback. Proof: `TestHeadAndPrefix` |
| 7 | `launcher/main.cpp` (~16562) `OfficialFetchFile` | download with `--retry 2`, cancel, progress by file size | **Moved.** `OwnDownload` with `retries = 2` (timeouts, connection trouble, 408/429/500/502/503/504, doubling wait that a cancel ends), then `Download()` after a clear refusal as after a curl failure. Proof: `TestDownload` |
| 8 | `launcher/VersionsNet.cpp` `DownloadFile` (Versions page) | `-L -f`, 5 redirects, https-only redirects, speed limit, max size, cancel, progress, disk full | **New:** own client first (opt-in), curl as the fallback. Proof: `tests/test_launcher_download_own.cpp` |
| 9 | `editor/main.cpp` `Download` (~1944, update download) | `curl -L -f` with no-cache headers, then PowerShell | **Moved** (Linux, opt-in): `OwnDownload` with the no-cache headers and the updater agent, then the old curl and wget path. A 404 or full disk ends it like `curl -f`. Proof: `TestDownload` (the `main.cpp` block itself is compiled but not run by a test) |
| 10 | `scripts/*.sh`, `.github/workflows/*.yml`, `deploy/voice-server/install.sh` | CI and release tooling: downloads, Supabase publish (`POST`, `DELETE`, `--retry-all-errors`, `-w '%{http_code}'`, `Authorization`), `versions.json` checks | Not shipped to users. **Left on curl on purpose** (build tooling, not a runtime dependency; a shell script cannot call the C++ client without a new command line tool, and CI runners have curl) |

`popen`, `system` and `RunCmd` are only the shells that start curl in rows 3 to 9 (kept as the fallback); no other HTTP client or library is in the tree (`grep` of `launcher/`, `editor/`, `engine/`, `shared/`, `tools/`, `scripts/`).

## 2. What those callers need against what the client does

| Need | Used by rows | Status | Proof |
|---|---|---|---|
| GET, POST, PATCH, DELETE, HEAD, any token method | 1, 3, 4, 8 | Done | `test_own_http.cpp` (`TestBuild`), `test_own_http_net.cpp` (`TestPlain`) |
| Caller headers (`apikey`, `Authorization`, `Content-Type`, `Cache-Control`, `Prefer`) | 1, 3, 9 | Done | `TestPlain` (`/hdrs`) |
| Body from a string or a file | 1 | Done (whole file read into memory; no streaming upload) | `TestNativeHttp` (POST body from a file) |
| Any HTTP status is an answer with its body | 1 | Done | `TestNativeHttp` (404 with body) |
| Redirects (301/302/303/307/308, relative, loop cap, https to http refused, credentials dropped across origins) | 3, 5, 8, 9 | Done | `test_own_http.cpp` (`TestFetch`), `TestPlain`, `TestHostile` |
| Redirect cap of 5 and "https only" redirects like the curl flags | 8 | Done (`maxRedirects`, https to http always refused) | `test_launcher_download_own.cpp` |
| Chunked responses | all | Done | `TestParse`, `TestPlain` (`/chunked`) |
| gzip and deflate responses | none today (we ask for identity) | **Done in this change, opt-in** (`NetOptions::acceptGzip`, in memory, capped, own inflate, CRC and Adler checked, bomb capped) | `test_own_http.cpp` (`TestInflate`, `TestContentCoding`), `TestDownloadFeatures` |
| brotli, zstd | none | Missing, not needed (we never ask for them) | none |
| Streaming to a file | 3, 5, 7, 8, 9 | Done | `TestNativeHttp`, `test_launcher_download_own.cpp` |
| Resume and ranges (`Range: bytes=N-`, 206, 416) | 6 (range), future resume | **Done in this change.** A 206 must carry a valid `Content-Range` that starts where we asked and match its `Content-Length`; an unasked 206 is refused; a 200 means start again; `RequestEx` appends to the file and leaves it alone on 404 and 416 | `TestRange`, `TestDownloadFeatures` |
| Progress callback and cancel | 5, 7, 8 | **Done in this change.** `FetchHooks::onProgress`, `RequestOptions::progress`, `RequestOptions::cancel` (polled every 200 ms, also while the server is silent, while connecting and while resolving) | `TestRange`, `TestDownloadFeatures`, `test_launcher_download_own.cpp` |
| Timeouts: connect, silence, total | 1, 5, 6, 8 | Done (connect 8 s per address, silence 30 s, total 15 min; per call through `timeoutMs` or `RequestOptions`) | `TestPlain`, `TestHostile`, `TestDownloadFeatures` |
| Name resolution timeout | all | **Done in this change** (helper thread, `resolveMs`, cancel). A resolver that never answers leaves its thread behind, but the caller is free | `TestDownloadFeatures` (only the "name does not exist" and "cancelled" cases; a hung resolver is not simulated) |
| Max download size (`--max-filesize`) | 5, 6, 8 | Done (`limits.maxBodyBytes`, `RequestOptions::maxBytes`; checked on `Content-Length` and while reading) | `TestPlain` (caps), `test_launcher_download_own.cpp` |
| Proxy from the environment (`HTTPS_PROXY`, `HTTP_PROXY`, `NO_PROXY`) with CONNECT | all | Done | `TestProxy`, `TestNoProxyList` |
| Proxy credentials, SOCKS, `https://` proxies, PAC, `ALL_PROXY` | corporate networks | Missing. A setting we cannot honour is an error (then curl runs), never a direct connection. Hostile proxies are tested: CONNECT refused with 403, 407, 502, a redirect, garbage, a hang-up, data before the TLS handshake, a tunnel that never speaks TLS; none of them leads to a direct connection or a kept connection | `TestProxy`, `TestProxyHostile`, `test_native_fetch.cpp` (`TestProxyRefusal`) |
| Cookies | none (the APIs use bearer tokens) | **Done in this change, off by default.** `RequestOptions::cookies` keeps a host-only jar for one call and its redirects (Path, Secure, Max-Age; a Domain other than the host drops the cookie; 64 cookies, 4 KB each; `Expires` is ignored, so a cookie lasts as long as the call); no disk, no sharing between calls | `test_own_http.cpp` (`TestCookies`), `TestHeadConditionalCookies` |
| Multipart upload (`-F`) | none | Missing, not needed (uploads are raw file bodies) | none |
| HTTP/2 | none (WinHTTP asks for it on Windows; the servers we use answer HTTP/1.1) | Missing, not needed for correctness | none |
| Keep-alive and connection reuse | launcher startup (many small calls), HEAD then GET to one host | **Done in this change** (`NetOptions::keepAlive`, on in `NativeHttp`). Per thread, at most 8 idle connections, 15 s idle limit; idempotent methods only (GET, HEAD, OPTIONS, PUT, DELETE ride on a kept connection, a POST never does); one retry on a connection found dead before the first byte of the answer; never reused after HTTP/1.0, `Connection: close`, a body that ends at close, or leftover bytes after the body; the new call gets its own deadline, silence limit and cancel check. Reuse over TLS is unverified against a real server (the `openssl s_server` we test with closes after each answer; the connection check is conservative and drops a connection with unread bytes) | `test_own_http.cpp` (`TestKeepAlive`, in memory), `test_own_http_net.cpp` (`TestKeepAliveNet`, `TestHostileNet`, TLS cases in `TestTls`) |
| HEAD requests | 4 | Done (no body is read whatever the headers claim, including a wrong or gzip `Content-Length`; a HEAD never creates or truncates an output file; HEAD follows redirects) | `TestHeadConditionalCookies`, `TestHeadAndPrefix` |
| Conditional requests (`If-None-Match`, `If-Modified-Since`) | future update checks | **Done in this change.** `RequestOptions::ifNoneMatch/ifModifiedSince`; a 304 comes back as `notModified`, an output file is left alone even with `fileOnSuccessOnly = false`; `etag` and `lastModified` of the answer are returned for the next call; values with a line break are refused before any traffic | `TestHeadConditionalCookies` |
| TLS 1.2+, SNI, chain and host name checks, no way to turn them off | all https | Done (Linux, system OpenSSL loaded at run time; not our own TLS) | `TestTls`, `TestNoVerifyBypass` |
| CA store: system default, `SSL_CERT_FILE`, `CURL_CA_BUNDLE`, `SSL_CERT_DIR` | all https | Done on Linux | `TestTls` |
| Windows certificate store | all https on Windows | **Missing, by decision.** Windows keeps WinHTTP, which uses the Windows store and proxy settings. An own client on Windows would need TLS (Schannel) and the store through `CertOpenSystemStore`, and that is TLS work next to the crypto rule (`docs/crypto.md`); it cannot be tested here (no Windows). | none |
| macOS TLS | Mac builds | Missing (macOS is parked) | none |
| Client certificates, pinning, OCSP | none | Missing, not needed | none |
| Windows | all | Not applicable: the own client is not compiled in on Windows | none |

## 3. What changed for readiness

- `OwnHttpInflate.hpp`: gzip and zlib decoder of our own (compression, not cryptography), wired behind `acceptGzip` and `Limits::allowGzip`; off unless asked for, so every existing test and caller behaves as before.
- Range handling in `Fetch`: strict `Content-Range` checks, `FetchResult::partial/rangeStart/rangeTotal`.
- `FetchHooks::onProgress`, `NetOptions::cancel`, `NetOptions::resolveMs`, a resolver on a helper thread.
- `okay::http::RequestEx` (progress, cancel, resume, size cap, timeouts, gzip) and a `timeoutMs` argument on `okay::http::Request`; the account layer now passes its per-call timeout to it (before, only the curl fallback honoured it, so with the own client on, the join-time ticket call could wait up to 15 minutes).
- `launcher/VersionsNet.cpp` `DownloadFile` tries the own client first (only when built with `OKAY_OWN_HTTP=ON` and run with `OKAY_OWN_HTTP=1`), then curl. Windows is untouched (`#if !defined(_WIN32)`).
- Tests of the first change: `test_own_http` 3,961 checks (was 147), `test_own_http_net` 181 (was 156), `test_launcher_download_own` 6.

### The second change: call sites and the missing pieces

- `engine/include/okay/Platform/NativeFetch.hpp`: three helpers with the behaviour of the curl commands they stand in for, each returning `NotHandled` for anything that is not a clear answer so the old curl line runs unchanged: `OwnDownload` (`-L -f --max-filesize --retry`, progress, cancel), `OwnHeadDump` (`-sIL`), `OwnFetchPrefix` (`-r` with `-m`). Callers in `launcher/main.cpp` (`HeadSize`, `RemoteSize`, `DlWorker`, `OfficialFetchFile`, link preview), `editor/main.cpp` (`Download`) and `PlayFab.hpp` (`Post`) are short `#if defined(OKAY_OWN_HTTP_ACTIVE)` blocks in front of the old code. No old line was changed.
- In the client: keep-alive with a safe retry, HEAD that never reads a body, conditional requests, a per-call cookie jar (off by default), `ExResult::rawHead`.
- `HEAD`, `204` and `304` answers no longer fail on a `Content-Encoding` they cannot decode or a bad `Content-Length` (there is no body to decode or frame).
- Tests of this change: `test_own_http` 4,028 checks, `test_own_http_net` 257, new `test_native_fetch` 54, new `test_playfab_own` 15, `test_launcher_download_own` 6 (unchanged). All also run clean under ASan and UBSan (`scripts/own_http_sanitized.sh` now includes the two new ones). The CI `own-http` job builds and runs all five; it has still never been seen running on GitHub.

## 4. The flip criteria (`docs/own_code_roadmap.md`, "How a library is removed", step 3)

| Criterion | Met? | Detail |
|---|---|---|
| 1. A real-use corpus passes (real servers, not only our own scripts) | **Not met** | Everything runs against local servers we wrote and `openssl s_server`. One manual run through the development sandbox's real proxy to one https host. Not run against GitHub Releases, Supabase, PlayFab or a CDN. The call sites added in the second change are tested through their helpers against loopback servers; the `main.cpp` blocks that call them were compiled (syntax check, with and without the option) but not run. A real-server run should cover: the launcher's version index and an installer download from the release host, a Supabase REST call with a 4xx body, a redirect through a CDN, a download behind a corporate-style proxy. |
| 2. Platform tests pass on Windows and Linux | **Not met** | Linux only, one machine, OpenSSL 3.0.13. The `own-http` CI job is written but has not been seen running on GitHub. There is no Windows run, and the client is not compiled on Windows at all. A Windows flip would first need the Schannel and certificate-store decision above. |
| 3. Fuzz under ASan/UBSan, soak for stateful code | **Partly met** | Parser mutation fuzz, 200,000 iterations clean (earlier change). The new inflate has its own checks (every truncation and every single bit flip of a gzip and zlib vector, a zip bomb, 3,000 random mutations per run) and ran clean under ASan/UBSan here with 20,000 parser iterations. It has not had a long fuzz run (the 200,000 run was not repeated) and there is no soak (hours of repeated downloads, thousands of connections, descriptor and thread leak checks). The resolver helper thread and the new per-thread connection pool are stateful pieces and have had no soak; the pool is covered by thread-exit, stale-connection, cap-of-8 and poisoned-connection tests only. |
| 4. No known bugs in our replacement | **Not met in the sense that matters** | No known crash or wrong answer. Known gaps that a real user can hit: no proxy credentials or SOCKS (those users fall back to curl), a hung resolver leaves a thread, TLS is only as good as the machine's OpenSSL, connection reuse over TLS has not been seen with a real server, a redirect from a link-preview URL to a private address is followed (curl did the same; the address check only covers the first host). |
| 5. Owner approval, written in the PR | **Not met** | Not asked. |

Also open from `docs/own_http_client.md`: the parser is new code with no outside review (the reason for the default staying OFF). All runtime curl uses on Linux (rows 1 to 9) now try the own client first; row 10 (scripts) is build tooling and stays.

## 5. How close the flip is (honest estimate)

Call sites on Linux: 9 of the 9 runtime places that used curl or HTTP now try the own client first (3 before this change: rows 1, 3, 8). Client features the callers need: all present except proxy credentials, SOCKS and HTTP/2 (not needed for correctness), and a Windows certificate store (a decision, not code). Flip criteria met: 0 of 5 fully, 1 partly (fuzz), 1 with no known bugs.

For Linux builds the code is about 85 percent of the way. What is missing is not code but evidence: a CI run on GitHub, real servers, a soak run, an outside look at the parser, and owner approval. Putting a number on the whole flip: about 40 percent, because four of the five criteria are evidence criteria and none of them has been started beyond local tests. It is not ready to flip and nothing should be flipped on this page's word.

For the whole product it is much further away: Windows is 0 percent for the own client (it needs a TLS answer first), and macOS is not started. Removing curl as a dependency also needs the call sites to stop falling back to it (one release after a flip, as `docs/own_code_roadmap.md` says), and Windows still uses PowerShell or curl as the fallback in the launcher and editor downloads.

## 6. Next steps, in order

1. Run `own-http` on GitHub and fix what it shows.
2. A real-server run (list in criterion 1), written down with dates. Include a kept-alive https connection to the release host and the API host.
3. A soak script (repeat downloads and API calls for an hour, watch descriptors and threads) and a 200,000-iteration fuzz run of the new inflate and of the cookie and keep-alive paths.
4. Run the launcher and editor with `OKAY_OWN_HTTP=1` on a real Linux desktop and exercise: game download with cancel, official game install, link preview, update download, PlayFab login. The `main.cpp` blocks are compiled but have never been run.
5. A small command line tool on the own client for the release scripts, only if we want the scripts off curl too (not needed for players).
6. Decide Windows (keep WinHTTP, the platform's own client, not third-party code) and macOS.
7. Outside review of the parser, then owner approval, then flip, then delete the curl lines one release later.
