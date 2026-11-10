# Data assets and data packs

Headers: `okay/Core/DataAsset.hpp`, `okay/Core/DataPack.hpp`.

## DataAsset

A `.okaydata` file is a small list of named fields, for item and enemy definitions and game settings that should live
outside code. The editor edits them; OkayScript reads them with `data_num`, `data_str`, `data_has`, `data_set` and
`data_save`.

```
# okaydata 2
name = Goblin
hp = 30
speed = 3.5
```

```cpp
DataAsset a;
a.Set("name", "Goblin");  a.Set("hp", "30");
a.Save("goblin.okaydata");               // written aside and swapped in: a crash or a full disk keeps the old file

DataAsset b;
if (b.Load("goblin.okaydata")) {         // false, fields untouched, if the file cannot be opened
    std::string name = b.GetString("name");
    int hp           = b.GetInt("hp", 10);
    bool boss        = b.GetBool("boss");            // true/false, yes/no, on/off, 1/0, any case
    double speed     = b.GetNumber("speed", 1.0);
    double armor;    if (!b.TryGetNumber("armor", armor)) { /* missing, or not exactly one number */ }
}
```

Fields keep their order. Saved files start with `# okaydata 2`; keys and values are escaped (backslash, newline,
carriage return, tab, `=` in a key, blanks at the edges, `#` at the start of a key), so any text round trips, including
multi-line values and empty keys and values. Files without that line (older ones, or written by hand) are read as plain
`key = value` lines. A UTF-8 byte order mark at the start of a file is skipped, so a file saved by an editor that adds one
still finds its first key.

### Numbers

`GetNumber` reads the leading number of the text (text without one reads 0, as it always did). A value that is infinite
or too big for a double (`inf`, `-Infinity`, `1e999`) and `nan` give your default instead of reaching a script and
poisoning every sum. `GetInt` rounds toward zero and clamps to the int range (a missing or unusable value gives the
default). `TryGetNumber` is the strict form: it succeeds only when the key exists and its whole text is one finite
number, so `12abc` is refused; use it where a typo in a data file should be noticed.

### The cache

`DataAsset::Cached(path)` loads a file once and returns the same object on every call (this is what the script
functions use).

- A file that cannot be opened is not remembered as empty: you get an empty asset to `Set()` into, and while it stays
  untouched every call tries the file again, so a file created later is picked up.
- A cached file is not re-read when it changes on disk. `DataAsset::Reload(path)` re-reads it (false, with the cached copy
  kept, if it cannot be read); the editor can call it after saving.
- `DataAsset::Invalidate(path)` and `InvalidateAll()` make the next `Cached()` read the file again. A reference an earlier
  call returned stays valid: the asset is emptied, not freed, and fills again on that next call (it used to dangle).
- Relative paths that are not found are retried under `DataAsset::SetBaseDir(dir)` (the player sets it to where the
  game's files live).
- Main thread only: the cache is not locked. Share a loaded `DataAsset` between threads read-only, or copy it.

## DataPack

Light obfuscation for shipped data (scenes, config) so players cannot casually open and edit it in a text editor. This
is not DRM: the key ships inside the game, so a determined attacker can still recover the data. The checksum detects
edits and damage.

```cpp
std::string packed = DataPack::Pack(sceneText);          // "OKP1" + nonce + checksum + keystream-XOR'd body

bool ok = false; std::string why;
std::string text = DataPack::Unpack(fileBytes, &ok, &why);   // plain text passes through; a pack is decoded
if (!ok) { /* damaged: `why` says what */ }
```

- Anything that starts with `OKP1` is a pack, even when it was cut off inside its 20-byte header (`IsPacked`). Decoding
  such a file, or one whose body no longer matches the checksum, reports damage (`ok == false`, empty text); it is never
  handed on as plain text, and nothing reads past the end of the data.
- `Unpack(data)` without the flag gives an empty string for damage; a valid pack of an empty file also unpacks to an
  empty string, so use the `ok` form (or `TryDecode`) when you need to tell them apart. The loaders check
  `IsPacked(raw) && text.empty()` for the same reason.
- Empty, binary (every byte value, NULs) and large inputs round trip. The keystream mixes once per 8 bytes.

## What changed (core wave 6)

| Was | Now |
| --- | --- |
| A file saved with a byte order mark lost its first key (`"<BOM>name"` was stored). | The mark is skipped when loading. |
| `GetNumber("nan")` returned NaN, `"inf"` and `"1e999"` returned infinity. | They give the default; NaN never comes out. |
| No typed getters. | `GetInt`, `GetBool`, `TryGetNumber`. |
| `Invalidate` / `InvalidateAll` freed the cached asset: a reference a script or panel still held dangled. | The asset is emptied and refilled in place; references stay valid. |
| A pack cut inside its header (`OKP1` plus fewer than 20 bytes) counted as plain text, so the loader tried to parse the raw bytes. | It is a damaged pack: `IsPacked` is true and `Unpack(..., &ok)` reports it. |
| `DataPack` header fields were read in two copies of a lambda that gave a compiler warning. | One `Get64` helper. |

Checked and found fine (the older audit notes listed two of these as open): a multi-line value, a key with `=`, `#`, edge
blanks or nothing at all, NUL bytes and 200 KB values all round trip through `Save` / `Load`; a file created after a failed
`Cached()` is picked up; every truncation and every flipped byte of a pack is caught.
