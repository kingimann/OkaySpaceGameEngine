# Our own HTTP client (`okay::ownhttp`)

Status: **side by side, off by default, Linux only for HTTPS.** The spawned `curl` is still the default on every system except Windows (where WinHTTP is used) and is still the fallback whenever the own client cannot be used. Nothing has been removed. This is the curl row of `docs/own_code_roadmap.md`.

## The decision, in one paragraph

We write the HTTP/1.1 client ourselves. We do **not** write TLS. TLS is taken from the platform: WinHTTP on Windows (unchanged), the system OpenSSL (`libssl`) on Linux, loaded at run time with `dlopen` so that nothing is linked and a machine without it simply keeps using curl. macOS is not wired (see NOT DONE). Writing our own TLS is out of scope for the same reason as our own crypto (`docs/crypto.md`): it needs an outside review we do not have, and a mistake there is a security hole.

## What it is

| File | What |
|---|---|
| `engine/include/okay/Platform/OwnHttp.hpp` | The pure layer: URL parser, request builder, response parser (status line, headers, Content-Length, chunked, close-delimited), redirects, limits. Talks to an abstract `Stream` and `Connector`; no sockets, no TLS, no OS headers |
| `engine/include/okay/Platform/OwnHttpNet.hpp` | POSIX transports: non-blocking TCP with deadlines, proxy settings from the environment, CONNECT tunnel, TLS through `libssl` loaded with `dlopen`, and `okay::ownhttp::Perform` that ties them together |
| `engine/include/okay/Platform/NativeHttp.hpp` | The callers' entry point (`okay::http::Request`, `NativeAvailable`). On POSIX it uses the own client only when built with `-DOKAY_OWN_HTTP=ON` **and** run with `OKAY_OWN_HTTP=1` |
| `engine/include/okay/Platform/OwnHttpInflate.hpp` | gzip and zlib decoder of our own (compression, not crypto), used only when a caller asks for gzip |
| `tests/test_own_http.cpp` | Unit tests, hostile inputs and mutation fuzz of the pure layer (standalone, no engine) |
| `tests/test_own_http_net.cpp` | Integration tests against local servers, a local proxy and `openssl s_server` |
| `tests/scripts/gen_http_test_certs.sh` | Makes the throw-away test CAs and certificates (valid, wrong host name, expired, untrusted CA) |
| `scripts/own_http_sanitized.sh` | Builds and runs both tests under AddressSanitizer and UndefinedBehaviorSanitizer |

All three headers are header-only. The only link-time addition is `libdl` (`${CMAKE_DL_LIBS}`), and only for builds that set `OKAY_OWN_HTTP`.

## Turning it on

- Build: `-DOKAY_OWN_HTTP=ON` (default `OFF`; ignored on Windows and macOS).
- Run time: environment variable `OKAY_OWN_HTTP=1`. Without it `okay::http::NativeAvailable()` is false and callers use curl exactly as before. `OKAY_HTTP_CURL=1` still forces curl.
- `OKAY_OWN_HTTP_LIBSSL=/path/to/libssl.so.3` replaces the library search list (no fallback to the default names when it is set).
- If the own client gets no HTTP response for any reason (libssl missing, certificate refused, proxy trouble, malformed answer, timeout), `okay::http::Result::reached` stays false, `Result::error` says why, and the existing callers run their curl path. Behaviour and signatures are otherwise those the callers already use: redirects followed, any HTTP status is `reached`, `outFile` streams to disk.

## HAVE / GAP

| Area | HAVE | GAP |
|---|---|---|
| HTTP/1.1 requests | Any token method, `Host`, `User-Agent`, `Accept-Encoding: identity`, `Connection: close`, `Content-Length` bodies, caller headers as `Name: value` lines | No keep-alive or connection pool (one TCP and TLS handshake per request, one per redirect hop); no `Expect: 100-continue`; no request body streaming (body is a string or a file read into memory); no chunked request bodies |
| Response parsing | Status line, headers, `Content-Length`, chunked (extensions and trailers read and ignored), close-delimited bodies, HEAD/204/304, interim 1xx skipped | Trailers are discarded; no HTTP/2 or HTTP/3; no brotli or zstd; gzip and deflate only when the caller opts in (otherwise `Content-Encoding` other than identity is an error, and we ask for identity) |
| Strictness | Bare LF or CR in a head, obsolete line folding, NUL, duplicate or conflicting `Content-Length`, `Content-Length` with `Transfer-Encoding`, unknown transfer encodings, `101` upgrades, overflowing sizes: all refused | A close-delimited body (no length, no chunking) over plain HTTP cannot be told from a truncated one; over TLS OpenSSL 3 reports a missing close_notify as an error, and we treat that as a failure |
| Redirects | 301/302/303/307/308, absolute, relative, protocol-relative, dot segments, hop cap (10), https to http refused, 301/302/303 turn a non-GET into GET and drop the body, credentials (any header whose name has auth, key, token, secret, cookie, password or session in it) dropped when the origin changes | No cookie jar, so no cookie handling across redirects; no `Refresh` header; no redirect policy callback |
| Injection | CR, LF, NUL and other controls in the URL, method, header names or values are refused before anything is sent; `Host`, `Content-Length`, `Transfer-Encoding`, `Connection`, `Accept-Encoding`, `Expect`, `Upgrade`, `TE`, `Trailer` cannot be set by callers; user info in a URL is refused | Callers must still percent-encode non-ASCII URLs (they are refused, not encoded) |
| Limits | Response head 64 KiB and 100 headers; body cap (64 MiB in memory, 8 GiB to a file by default in `NativeHttp`); connect timeout (8 s per address), silence timeout (30 s), total timeout (15 min, covers all redirects and proxy and TLS setup) | A hung resolver leaves a helper thread behind (the caller is released by `resolveMs`) |
| TLS (Linux) | `libssl` 1.1.0 or newer loaded with `dlopen` (`libssl.so.3`, `libssl.so.1.1`, `libssl.so`); TLS 1.2 or newer; SNI; chain verification (`SSL_VERIFY_PEER`); host name check with `SSL_set1_host` (no partial wildcards) or IP check with `X509_VERIFY_PARAM_set1_ip_asc`; verify result and peer certificate checked again after the handshake; refused (no connection made) when libssl cannot be loaded | No client certificates; no session resumption or ALPN; no OCSP, CRL or certificate pinning beyond what the system store and OpenSSL do; one `SSL_CTX` per CA setup is kept for the life of the process |
| CA store | `SSL_CERT_FILE`, `CURL_CA_BUNDLE` (files), `SSL_CERT_DIR`; else the system default store. A variable that is set but unusable is an error, never a silent fall back to something else | No Windows or macOS keychain integration in this client (Windows uses WinHTTP, which has its own) |
| Proxies | `HTTPS_PROXY` / `https_proxy` for https (CONNECT tunnel, TLS end to end with the origin), `http_proxy` / `HTTP_PROXY` for http (absolute-form request), `NO_PROXY` / `no_proxy` (suffixes, `*.` and leading dot, exact names and IPs, IPv4 CIDR, `*`); a proxy setting we cannot honour is an error, never a direct connection | `https://` proxies (TLS to the proxy), SOCKS, proxy credentials, `ALL_PROXY`, auto-configuration (PAC), IPv6 CIDR in `NO_PROXY` |
| Compression | Opt-in `acceptGzip` (`NetOptions`, `FetchRequest`): `Accept-Encoding: gzip, deflate`, `Content-Encoding` gzip, x-gzip and deflate (zlib) decoded by our own inflate; encoded body capped (64 MiB, held in memory), decoded body capped by `maxBodyBytes`, CRC-32 and size (gzip) or Adler-32 (zlib) must match, no trailing bytes, no stacked codings | brotli, zstd; no streaming decode (the encoded body is held whole); off by default and ignored for downloads to a file |
| Range and resume | `Range: bytes=N-` as a caller header; a 206 must have a valid `Content-Range` that starts at N and a matching `Content-Length`, an unasked 206 is refused (`FetchResult::partial`, `rangeStart`, `rangeTotal`); `okay::http::RequestEx` resumes into a file (`resumeFrom`), restarts on a 200, leaves the file alone on 404 and 416 | Multiple ranges, suffix ranges (`bytes=-N`, passed through but not validated), `If-Range` (callers may set it themselves) |
| Progress, cancel, timeouts | `FetchHooks::onProgress` (false stops), `NetOptions::cancel` (polled every 200 ms while connecting, resolving, waiting and reading), per-call `timeoutMs` on `okay::http::Request`, `RequestOptions` (`maxBytes`, `maxRedirects`, `connectMs`, `ioMs`, `totalMs`) | Windows keeps WinHTTP (only `timeoutMs` reaches it, unverified there) |
| Name resolution | `getaddrinfo` on a helper thread with `resolveMs` (8 s), the overall deadline and cancel; IP literals skip the lookup | A resolver that never answers leaves its helper thread behind until the process ends |
| Platforms | Linux and other POSIX with `dlopen`: pure layer, TCP, proxy, TLS. Windows: unchanged WinHTTP path. The pure layer and TCP/proxy code compile everywhere POSIX | macOS TLS (see NOT DONE) |

## Tests

- `own_http` (`tests/test_own_http.cpp`): 147 checks on URLs, `Location` resolution, the request builder, about 40 hostile responses (bare LF/CR, folding, NUL, bad and overflowing lengths, chunk size abuse, smuggling shapes, truncation, header floods, body caps for all three framings), redirects through a scripted connector (chain, loop cap, https to http, method change, credential stripping, hooks), and a mutation fuzz: byte flips, deletions, insertions of dictionary tokens, truncations and swaps over a corpus of valid responses, each parsed in one piece and again in tiny pieces (the verdict must not depend on how the bytes arrive), plus random URLs and Locations whose results must stay free of control characters. `scripts/own_http_sanitized.sh 200000` runs 200,000 iterations under ASan and UBSan, clean.
- Added with `docs/own_http_readiness.md`: `own_http` is now 3,961 checks (inflate: vectors from zlib, every truncation and bit flip, zip bomb capped, bad distances and tables, trailing bytes, 3,000 random mutations; Content-Encoding framing in pieces and chunked; Content-Range and Range parsing and the 206 rules; progress and abort), `own_http_net` 181 (gzip and Range over real sockets, resume into a file, progress, cancel from the callback and against a silent server, cancel before connecting, `timeoutMs`, name resolution), and `test_launcher_download_own` (6 checks: the launcher's Versions download through the own client against a loopback server). Run under ASan and UBSan with `scripts/own_http_sanitized.sh 20000` (the 200,000 run was not repeated for the new code).
- `own_http_net` (`tests/test_own_http_net.cpp`): 156 checks before that change. Plain HTTP server (ok, echo, 404, chunked, redirects relative and absolute, redirect loop, cross-origin credential stripping, 3 MiB body with a cap above and below it for Content-Length and chunked, streaming to a callback, slow server, hanging server, dripping server against the total limit, truncated body, garbage, gzip refusal, header and URL injection refused before any connection, refused connection). A tiny local proxy (absolute-form, CONNECT, 403 from the proxy, `NO_PROXY` bypass, unusable proxy settings are errors). `NativeHttp` wiring (opt-in switch, POST body from a file, download to a file, fall-back signalling). TLS against `openssl s_server -HTTP` with generated certificates: valid certificate by name and by IP, wrong host name, expired, signed by an untrusted CA, trusting a different CA, missing CA file, CA from `SSL_CERT_FILE` and `CURL_CA_BUNDLE`, no CA configured, SNI sent (the server picks its certificate by SNI), TLS 1.1 server refused, https to a plain HTTP server, https to `127.0.0.2` against a certificate for `127.0.0.1`, https to http redirect, handshake that never completes, https through the CONNECT proxy including a bad certificate behind it, and libssl missing (https refused and no connection made). The TLS cases print `SKIP` and the test still passes when libssl or the `openssl` program is missing; `OKAY_REQUIRE_TLS_TESTS=1` turns a skip into a failure (use it in CI).
- Hostile servers over real sockets (`TestHostile`, in `own_http_net`): a header line that never ends, 150 headers (count cap), 90 headers of 1 KiB (byte cap), a 6,000 byte reason phrase, 40 interim `102` responses, 400 trailer lines, an unframed body that never ends and a chunked body of endless 1-byte chunks (both stop at a tightened body cap, the bytes handed to a streaming caller never pass the cap, and the run ends within seconds), a caller that aborts a download, a redirect loop (exactly 4 requests with `maxRedirects = 3`, exactly 11 with the default 10), a 20,000 byte `Location`, a redirect that carries a 100 MiB body, silence in the middle of a head and in the middle of a body (`ioMs`) and a stalled body under the total limit. Mutation check by hand: switching off the header-count check and the trailer cap makes three of these fail.
- Verification can not be turned off (`TestNoVerifyBypass` and the wrong-host case in `TestTls`): a server with a wrong-host certificate is still refused when the environment carries `OKAY_OWN_HTTP_INSECURE`, `OKAY_HTTP_INSECURE`, `CURL_INSECURE`, `SSL_NO_VERIFY`, `NODE_TLS_REJECT_UNAUTHORIZED=0`, `PYTHONHTTPSVERIFY=0`, `GIT_SSL_NO_VERIFY` and similar names; and a scan of `OwnHttpNet.hpp` fails the test if `VERIFY_NONE`, `SSL_set_verify` or a certificate verify callback appears, or if `SSL_CTX_set_verify` is called other than once with `SSL_VERIFY_PEER`. The scan is a tripwire, not a proof.
- CI: the `own-http` job in `.github/workflows/ci.yml` (not required, 15 minutes) configures with `-DOKAY_OWN_HTTP=ON`, checks the define reaches the engine, builds only `test_own_http` and `test_own_http_net`, runs them with `OKAY_REQUIRE_TLS_TESTS=1` (a skipped TLS case fails) and then `scripts/own_http_sanitized.sh 100000` (ASan and UBSan). It has not yet run on GitHub (written and checked locally only: YAML parses, the same commands pass here).
- Mutation check done by hand: removing the `SSL_set1_host` call makes the wrong-host-name test fail, so that test does test the check.
- Also run once against the real proxy of the development sandbox (CONNECT to an https host, chain checked against the proxy's CA from `SSL_CERT_FILE`), and with a bogus `SSL_CERT_FILE` (refused).

## Threat model

What this code verifies (and refuses to proceed without):

- The server's certificate chains to a CA in the configured trust store, is within its validity dates, and is valid for the host name (or IP address) in the URL, by OpenSSL's own checks. The same is checked again after the handshake. Nothing turns these off: there is no option, environment variable or retry that skips verification, and a failed verification never falls through to plain HTTP.
- TLS 1.2 or newer.
- Every byte we send in a header, URL or method is free of CR, LF, NUL and other control characters; callers cannot add headers that change message framing.
- Every byte we accept in a response head follows the strict rules above, sizes cannot overflow, and bodies and heads are capped, so a hostile server can cost at most the caps and the time limits.
- Credentials are not forwarded to a different origin on redirect, and a redirect from https to http is refused.
- A proxy only ever sees a CONNECT line (host and port) for https, never the request; the TLS session is end to end with the origin and verified against the origin's name.

What is trusted, and therefore outside what this code can guarantee:

- **The system CA store** (or the file or directory named by `SSL_CERT_FILE`, `CURL_CA_BUNDLE`, `SSL_CERT_DIR`). Whoever can write those, or the environment variables, can make us trust any CA. An environment that sets them (for example a corporate or sandbox proxy that re-signs TLS) is trusted by design.
- **The OpenSSL library found on the machine** (libssl and the libcrypto behind it): its TLS state machine, its X.509 parsing and verification, its random numbers. We load whatever the loader finds first among the default names; `OKAY_OWN_HTTP_LIBSSL` and the library search path decide which one. A vulnerable or replaced library is a vulnerable client. We do not pin versions beyond requiring 1.1.0 or newer.
- **The proxy**, for availability and for seeing which host is being contacted (not content).
- The operating system's sockets, name resolution and clock (certificate dates).
- Our own parser is new, unreviewed code. It has unit tests, hostile-input tests and fuzzing under sanitizers, but no outside review. This is the reason the own client is off by default.

## NOT DONE

- **macOS:** no TLS. `OwnHttpNet.hpp` compiles there (TCP, proxy), but `TlsApi::DefaultNames()` is empty and `NativeHttp` does not enable the client on Apple systems, so macOS keeps spawning curl. A real answer needs Network.framework or Secure Transport (Objective-C or C APIs), which could not be tested here.
- **Windows** is unchanged (WinHTTP); the own client is not used there.
- HTTP/2 and HTTP/3, keep-alive and connection pooling, request body streaming, `Expect: 100-continue`.
- brotli and zstd response bodies; streaming gzip (the encoded body is held in memory).
- Cookies (no jar), `Set-Cookie` handling, authentication schemes (Basic, Digest, Negotiate), `Authorization` is only passed through as a caller header.
- Proxy credentials, `https://` and SOCKS proxies, PAC files, `ALL_PROXY`.
- Client certificates, certificate pinning, OCSP stapling checks, ALPN, session resumption, TLS 1.3 early data (not used).
- A hung resolver is bounded by `resolveMs`, but only the "name does not exist" and "cancelled" cases are tested; a resolver that never answers is not simulated.
- The connect timeout (`connectMs`) is not tested against a black-holed address (it needs a non-local network); the silence and total limits are.
- Memory: the response head and chunk lines are capped, but a body into memory is held whole up to the cap (64 MiB); a download to disk streams, with the cap at 8 GiB. The Windows and curl paths have their own limits and were not touched.
- The `own-http` CI job has not run on GitHub yet; no run with OpenSSL 1.1.x; no clang or MSVC build of the tests; no sanitizer run of the engine-linked `NativeHttp` path (the net test includes `NativeHttp.hpp` standalone).
- Replacing the other curl uses: `launcher/VersionsNet.cpp` `DownloadFile` now tries the own client first (opt-in); the launcher's cancellable game and official-game downloads, size probes and link preview, the editor's update download and PlayFab still spawn curl (the launcher's `Download` helper tries `NativeHttp` first); `scripts/*.sh` are build tooling and stay. Full list and plan: `docs/own_http_readiness.md`.
- Windows certificate store and Windows at all: Windows keeps WinHTTP. A readiness table and the flip criteria are in `docs/own_http_readiness.md`.
- Making it the default, and dropping curl from the docs and the install notes. That needs the outside review above and a period of the opt-in being used without fallbacks.
- Tested only against OpenSSL 3.0.13 on Linux. OpenSSL 1.1.x is supported in the code (symbol names, minimum version through `SSL_CTX_ctrl`) but was not run.
