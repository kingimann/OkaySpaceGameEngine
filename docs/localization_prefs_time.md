# Localization formats, typed Prefs, fixed timestep

Core additions in `okay/Core/Localization.hpp`, `Prefs.hpp` and `Time.hpp`. Tests: `tests/test_loc_prefs_time.cpp` and `tests/test_core_audit.cpp`.

## Plural rules

`Localization::Plural(lang, n)` returns a CLDR category (zero, one, two, few, many, other) on the absolute value of `n`.

| Language | Rule |
| --- | --- |
| en, de, nl, sv and others | 1 is one, everything else other |
| fr, pt (pt-BR) | 0 up to (not including) 2 is one; whole millions many |
| pt-PT | exactly 1 is one; whole millions many |
| es, it, ca | 1 is one; whole millions many |
| ru, uk, be | one (1, 21...), few (2-4, 22-24...), many (0, 5-20, 25...), fractions other |
| pl | 1 one; 2-4 (not 12-14) few; other whole numbers many; fractions other |
| cs, sk | 1 one; 2-4 few; fractions many; else other |
| ar | 0 zero, 1 one, 2 two, 3-10 (mod 100) few, 11-99 many, else other |
| hr, sr, bs | one (1, 21, 101...), few (2-4, 22-24...), other (0, 5-20, 11-14, 25...); fractions other |
| sl | one (1, 101...), two (2, 102...), few (3, 4, 103... and fractions), other |
| lt | one (1, 21...), few (2-9, 22-29...; not the teens), other; fractions many |
| lv | zero (0, 10, 20, 11-19...), one (1, 21...), other |
| ro | 1 one; 0 and 2-19 (mod 100) few; other; fractions few |
| he | 1 one, 2 two, other; fractions below 1 one |
| hi, bn, fa, gu, kn, zu, am | 0 and 1 are one (fractions below 1 too), else other |
| cy, ga, mt | Welsh (0 zero, 1 one, 2 two, 3 few, 6 many), Irish (1, 2, 3-6 few, 7-10 many), Maltese |
| is, mk, da | is and mk: one for 1, 21, 31... (not 11); da: 1 is one, fractions 0 to 2 too |
| ja, zh, ko, vi, th, id, ms, lo, my, km, jv | always other |

`SetPluralRule(lang, fn)` still overrides any of them.

`Localization::PluralText(key, count, extraArgs)` looks the text up in the string table as `key.<form>` (`coins.one`, `coins.few`...). Order: the form in the current language, `key.other` in the current language, then the same two in the default language, then `key` itself as an inline `{0, plural, ...}` pattern. `{count}` and `{0}` are the number; extra args are `{1}`, `{2}`. Pseudo-localization applies as for Get.

## Fallback chain

`Get`, `TryGet`, `Localize` now fall back through the parents of the language -> default language -> key (`pt-BR` -> `pt` -> default; `zh-Hant-TW` -> `zh-Hant` -> `zh` -> default), each miss reported once. A language never borrows from a sibling script (`zh-Hans` does not fall back to `zh-Hant`). `LocArg` integers keep their exact digits (a 64-bit id used to be rounded through a double).

## Formatting

`FormatNumber(value, decimals, lang)`, `FormatCurrency(amount, "EUR", lang)`, `FormatPercent(fraction, decimals, lang)`, `FormatDate(y, m, d, Short|Long, lang)`, `FormatTime(h, m, lang)`. An empty `lang` means the current language. Separators and symbol placement are built in for en, de, fr, es, it, pt (BR / PT), ru, pl, cs and similar, ja, zh, ko; unknown languages format like English. Currency decimals follow the currency (JPY, KRW none; KWD three). A value that rounds to zero never prints `-0`. These are formatting helpers, not a full CLDR: month names are built in for en, de, fr, es, pt, ru, pl.

## Typed Prefs

`SetBool/GetBool`, `SetVec3/GetVec3`, `SetColor/GetColor` join the int / float / string accessors; every getter takes a default for a missing or unparsable key. `HasKey`, `DeleteKey`, `DeleteAll` are aliases of `Has`, `Delete`, `Clear`. Values stay text in the file (`1`/`0`, `x,y,z`, `r,g,b,a`) so the file format is unchanged and older builds read it.

Prefs files are written whole or not at all (temp file, flush, rename), keys in order, and the previous good file is kept as `<file>.bak`. `Load` reads CR LF files, refuses a cut-off or zero-filled file and then reads `<file>.bak` (`Prefs::LastLoadUsedBackup()`; `Prefs::LastError()` says why a load or save failed or fell back). Every `Prefs` call may be made from any thread; a listener runs on the thread that made the change, outside the lock. See [save_games.md](save_games.md#what-changed-hardening-pass).

`Prefs::AddListener(fn)` / `RemoveListener(id)`: `fn(key)` runs after a Set changes a value (setting the same value is silent), after a delete, once per key on `DeleteAll`, and with `""` after `Load`.

## Fixed timestep

`Time::FixedDeltaTime()` (default 0.02) and `SetFixedDeltaTime`. `FixedTimestep` is the accumulator for gameplay code:

```cpp
FixedTimestep fixed;   // step from Time::FixedDeltaTime(), 5 steps at most per frame
fixed.Run(Time::DeltaTime(), [&](float dt) { Simulate(dt); });
Draw(Lerp(prev, cur, fixed.Alpha()));
```

Feed it `Time::DeltaTime()` to follow the time scale and pause, or `UnscaledDeltaTime()` to ignore them. Time beyond `MaxSteps` is dropped (`DroppedTime()`), not owed. It uses the same scheme as `Scene::StepPhysics` (accumulate, cap, step, alpha); scene physics is untouched and a test checks both give the same step counts.

## Core audit fixes

- `Mathf::SmoothDamp` with `deltaTime == 0` (paused) made the velocity NaN; it now returns `current`.
- `Mathf::Repeat` / `PingPong` with length 0 gave NaN.
- `Quat::LookRotation` with forward parallel to up (looking straight up/down) was degenerate; zero forward gives Identity.
- `Quat::Normalized` of a NaN quaternion passed the NaN on; now Identity.
- UI markup colors (`UIDocument`) wrapped byte values (`300` became 44, `-1` 255); they clamp.
- New `Color::Parse` ("#RGB", "#RRGGBB", "#RRGGBBAA", "r,g,b[,a]").
