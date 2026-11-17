---
id: 10
group: switches
row: | 10 | Own HTTP client (off by default) | none | **Cannot be tried on Windows** |
---
### 10. Own HTTP client

Cannot be tried on Windows today. Windows already uses WinHTTP (the system's own client, `http=winhttp`), our client is
not compiled into Windows programs, and `OKAY_OWN_HTTP=1` does nothing there. It exists for Linux builds only
(`-DOKAY_OWN_HTTP=ON` at build time, then `OKAY_OWN_HTTP=1`; the line then says `http=own`). To try it on Windows we
would need our own TLS, which is cryptography and waits for the outside review (rule 18). Tested on main on Linux only:
its test programs (`own_http`, `own_http_net`).

What was added on 2026-11-16 (retries with backoff and `Retry-After`, downloads to a temporary file with SHA-256 while
streaming, resume after a dropped connection, atomic rename) is in the same place: Linux builds only, not in the
Windows release. On Windows nothing changed; the launcher's Versions download and the editor's update check use WinHTTP
and curl exactly as before.

On a Linux build (`cmake -DOKAY_OWN_HTTP=ON`, launcher and editor built), to try it:

1. Start the launcher with `OKAY_OWN_HTTP=1 ./OkaySpace` (the launcher). Open **Versions** and download a version. Expect:
   the download runs, a `<file>.part` file grows next to the target and is renamed to the real name only when the whole
   file has arrived; the version then installs as with curl. Pull the network cable (or stop Wi-Fi) for a few seconds
   in the middle: the download continues where it stopped (up to two more tries) instead of starting over.
2. Start the editor with `OKAY_OWN_HTTP=1 ./OkayEngine` and use **Engine > Check for Updates**. Expect: the version check and
   (if there is an update) the download work as before. With `OKAY_HTTP_CURL=1` as well, curl is used instead.
3. Without a desktop: build `test_own_http_download` and run it (`ctest -R own_http_download`), or compare with curl:
   `python3 scripts/own_http_bench.py build/tests/test_own_http_download 200 3` (prints a table and MET or NOT MET).

Unverified: steps 1 and 2 have not been run on a desktop here (the new code is covered by the loopback tests and the
launcher and editor files were only syntax-checked).

