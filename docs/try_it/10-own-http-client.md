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
