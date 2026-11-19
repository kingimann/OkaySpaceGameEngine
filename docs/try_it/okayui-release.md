---
id: okayui-release
row: | okayui-release | The two OkayUI programs in the Windows release (OkaySpaceOkayUI.exe, OkayEngineOkayUI.exe) | Windows zip / installer; Settings > Experimental > Run on our own UI | Never cross-compiled here; a build failure of the extras only prints a warning |
---
## The OkayUI programs in the Windows release

`scripts/build-windows-dist.sh` builds `OkaySpaceOkayUI.exe` (top level, beside `OkaySpace.exe`) and `OkayEngineOkayUI.exe` (`Tools\`) after everything else, so they are in the zip and in the installer. How to try them from the normal Windows release: start `OkaySpace.exe`, open Settings > Experimental and press Restart on "Run on our own UI (restart into OkaySpaceOkayUI.exe)". They are not in the update checksum list (the updater keeps replacing only the normal three programs); a build failure of the extras only prints a warning and the release goes out without them; set `OKAY_OKAYUI_APPS=OFF` in the build environment to skip them. Not run: the Windows cross-compile of these two programs was never done here.
