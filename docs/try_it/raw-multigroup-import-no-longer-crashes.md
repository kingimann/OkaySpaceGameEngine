---
id: raw-multigroup
---
## Importing a `.raw` file with several groups no longer crashes (bug fix, branch `claude2/own-readers-speed-3`, not released yet)

The Windows release reads `.raw` (raw triangle text) through Assimp, and Assimp 5.4.3 breaks on a file with two or more groups
of triangles: the import crashed. Such a file now goes to our own RAW reader. To try it once released: save this as `two.raw`

```
first
0 0 0  1 0 0  0 1 0
second
0 0 1  1 0 1  0 1 1
```

and import it in the editor. Expect two parts (`first`, `second`) and a log line "... more than one group of triangles, which
Assimp 5.4.3's RAW importer cannot read safely; reading it with our own RAW reader". A `.raw` file with one group still goes to
Assimp. Not tried on Windows (checked on Linux in a build with Assimp). Details: `docs/own_raw_reader.md`.
