Changelog entries (one file per change). Read docs/release/fast_release.md.

Add dist/changelog.d/<id>.txt in your own PR. <id> is the PR number or a short
name nobody else uses, for example 953.txt or ci-speed.txt. Format:

    section: Fixes and new things     (optional; the heading the bullets go under)
    pr: 953                           (optional; PR numbers, shown in the ROADMAP row)
    roadmap: one line for ROADMAP.md  (optional; the default is the first bullet)
    ---
    - What changed, in plain words a player can read. Wrap at about 75
      characters; a second line of the same bullet starts with two spaces.

Rules: the text must start with "- "; no blank lines; no tabs. Check it with
    python3 scripts/fast_release.py check-entries
The release workflow puts every entry that the last release did not have into
dist/CHANGELOG.txt, the update card and the GitHub release. Never delete or
rename an entry that was released; to correct one, write a new entry.
