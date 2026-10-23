# Releasing an update

Launchers and editors look for updates in two places and use whichever has the
newer version:

1. **Supabase Storage**: the public `releases` bucket on the project in
   `CMakeLists.txt` (`OKAY_DEFAULT_ACCOUNT_URL`). Checked first.
2. **GitHub Releases**: `releases/latest/download/<file>`. The fallback.

So an update can ship without the GitHub workflow: build, then publish to
Supabase.

## Publish to Supabase

```bash
# 1. Bump the version in CMakeLists.txt (project(... VERSION x.y.z)).
# 2. Build the Windows files (MinGW cross-compile; splits the editor's debug info).
scripts/build-windows-dist.sh
# 3. Upload. Either key works; it is read from the environment only.
SUPABASE_ACCESS_TOKEN=sbp_...   scripts/publish-supabase.sh   # management token
SUPABASE_SERVICE_KEY=eyJ...     scripts/publish-supabase.sh   # or the service_role key
```

The bucket layout:

| Path | What |
| --- | --- |
| `VERSION.txt` | newest version, uploaded last (cached 30 s) |
| `NOTES.txt` | "what's new" lines for the launcher (default: recent commit subjects; set `NOTES_FILE` to choose) |
| `v<ver>/OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `CHANGELOG.txt` | that version's files |

Only the newest 3 versions are kept (`KEEP=n` to change). `DRY_RUN=1` checks
the build and key without uploading.

Limits: on the current plan each file must be at most 50 MB. The editor is
about 23 MB because its debug info is split into `build-win-symbols/OkayEngine.debug`
(keep that file to decode crash addresses; it isn't uploaded to Supabase).
Storage and download traffic count against the Supabase plan's quotas.

Never commit either key. For a Claude cloud session, add `SUPABASE_ACCESS_TOKEN`
in the environment's settings (Network secrets, or an environment variable).

## GitHub Releases (still supported)

The `Release Windows build` workflow builds, makes the zip + installer and
publishes a GitHub Release. If the repo has a `SUPABASE_SERVICE_KEY` secret it
also runs `scripts/publish-supabase.sh`, so both places stay in step.

Launchers older than v2.794.0 only know about GitHub, so the first release that
contains the Supabase updater must also go out through GitHub Releases.
