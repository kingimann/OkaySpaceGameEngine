---
id: packaging
group: appendix
---
## How the packaging was checked (and what was not)

* `scripts/build-windows-dist.sh` now copies the `examples\` folders listed in `EXAMPLE_DIRS` (and `grid.png` for the OBJ
  samples) into `build-win-dist/examples/`. The zip step in `release.yml` and `scripts/release-local.sh` run
  `cp -r build-win-dist/*`, and the installer script `scripts/okayspace-installer.nsi` installs `build-win-dist/*`
  recursively (`File /r`), so both pick the folder up with no further change. The uninstaller now removes `examples\`.
* Run here: `bash -n` on the script, and the new copy block on its own against the real source tree, then a listing of
  the result. **Not run:** the full Windows cross build, the zip, `makensis`, the installer, anything on Windows.
* `release.yml` was not changed and no workflow was run.
