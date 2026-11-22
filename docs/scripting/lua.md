# Lua scripts (okay::lua)

Internal record of our own Lua interpreter and how `.lua` scripts bind to the engine. Feature map row 13.3.

## Where it came from

Everything here was written from the **Lua 5.4 Reference Manual** (the language and library specification) and
general compiler and interpreter techniques. No code from the reference implementation, LuaJIT, MoonSharp, NLua or any
other Lua implementation was used, ported or followed (CLAUDE.md rule 19a). Per file:

| File | Written from |
| --- | --- |
| `engine/include/okay/Lua/Lua.hpp` | manual 2.1 (values and types), 2.4 (metatables), 2.6 (coroutines); our own object layout |
| `engine/src/Lua/LuaCode.hpp` | our own bytecode design: a stack machine whose bottom slots are the locals |
| `engine/src/Lua/LuaCompiler.cpp` | manual 3.1 (lexical conventions), 3.2-3.5 (variables, statements, expressions, scoping), 3.4.8 (precedence), 9 (complete syntax); the "expression descriptor" technique of one-pass compilers |
| `engine/src/Lua/LuaState.cpp` | manual 2.1, 2.4, 2.5 (we do a plain stop-the-world mark and sweep; 2.5.4 weak tables), 2.6, 3.3.5 (for loops), 3.3.8 (to-be-closed variables), 3.4.1-3.4.4 and 3.4.7 (arithmetic, conversions, comparisons, length); open-addressing hash tables; FNV-1a string hash |
| `engine/src/Lua/LuaLibBase.cpp` | manual 6.1 (basic functions), 6.2 (coroutine), 6.7 (math), 6.9 (os, safe part only); xoshiro256** and splitmix64 from their published algorithm descriptions |
| `engine/src/Lua/LuaLibString.cpp` | manual 6.4 and 6.4.1 (string functions and patterns); our own backtracking matcher |
| `engine/src/Lua/LuaLibTable.cpp` | manual 6.6 (table functions); our own quicksort |
| `engine/src/Lua/LuaLibUtf8.cpp` | manual 6.5 (UTF-8 support) and the UTF-8 encoding rules |
| `engine/src/Lua/LuaScriptVM.cpp` | the engine's `IScriptVM` interface and the typed-language binding table (`engine/src/Script/EngineApi.cpp`) |

Why bytecode and not a tree-walker: coroutines must not use operating system threads (rule 21a). With an explicit frame
stack a yield saves the frames and returns; resuming continues the same loop. The instruction counter for the run budget
also has one natural place.

## Language and library

Supported: the whole Lua 5.4 syntax (locals, `<const>`, `<close>`, multiple assignment and results, varargs,
closures, numeric and generic `for`, `goto` and labels, integer and float subtypes with `//` and the 5.4 conversion
rules, all string escapes, long strings and comments), metatables with `__index`, `__newindex`, `__call`, the
arithmetic and bitwise events, `__eq`, `__lt`, `__le`, `__len`, `__concat`, `__unm`, `__close`, `__tostring`, `__name`,
`__metatable`, `__pairs`, `__mode`; coroutines (`create`, `resume`, `yield`, `status`, `wrap`, `isyieldable`, `running`,
`close`); `error`, `pcall`, `xpcall` with "chunk:line:" positions and variable names ("attempt to index a nil value
(global 'foo')"); `load` for text chunks (with an environment).

Libraries (sandboxed): base (no `dofile`, `loadfile`, `require`), `string` (all functions but `pack`, `unpack`,
`packsize`, `dump`), `table`, `math`, `coroutine`, `utf8`, `os.clock`, `os.time`, `os.date`, `os.difftime`. Not
present: `io`, `os.execute`, `os.exit`, `os.getenv`, `os.remove`, `package`, `debug`.

Not supported (documented differences):
- `__gc` finalizers: ignored. Weak tables (`__mode` "k", "v", "kv") work, but weak keys are not full ephemerons
  (a value that refers to its own key keeps the entry alive); strings are never removed, as the manual says.
- Yielding across `pcall`, `xpcall`, a metamethod, `table.sort`'s comparator or `string.gsub`'s function: raises
  "attempt to yield across a C-call boundary" (like Lua 5.1). Yielding from Lua functions, `for` iterators and
  `coroutine.wrap` generators works.
- Binary chunks: `load` refuses them.
- String comparison is by bytes (the C locale), character classes are ASCII.

Limits: an instruction budget (`State::SetBudget`; scripts get 20 million instructions per callback) that `pcall` can
not catch, a memory limit (`State::memoryLimit`, 256 MB), 200 nested native calls, 200 000 frames, 1 000 000 stack
slots, 200 syntax levels, strings up to 1 GB. Pattern matching and big string building count against the budget.

## Scripts on objects

A script returns a table. Fields holding a number, string, boolean or Vector3 are Inspector settings; functions named
like the lifecycle run by name with the table as `self`: `Awake`, `OnEnable`, `Start`, `Update(dt)`, `LateUpdate`,
`FixedUpdate(dt)`, `OnDisable`, `OnDestroy`, and engine events in either spelling (`on_trigger_enter` or
`OnTriggerEnter(other)`; `other` is the other GameObject). A script that returns nothing uses its global functions
(old scenes: `function update(dt)`), with lowercase names allowed.

Engine access, all from the binding table `Script/EngineApi`:
- `self.gameObject`, `self.transform` (also globals `gameObject`, `transform`), `GetComponent("Rigidbody")`,
  `obj:GetComponent(...)`, `Destroy(obj)`.
- Instance members: `obj.prop`, `obj.prop = v`, `obj:Method(args)` (overloads chosen by argument count and types).
- Static types are global tables: `Input.GetKey("space")`, `Time.deltaTime`, `Debug.Log(x)`, `GameObject.Find("A")`,
  `Vector3.up`, `Vector3.Distance(a, b)`.
- `Vector3.new(x, y, z)` or `Vector3(x, y, z)`; `v.x`, `v.magnitude`, `v.normalized`, `+ - * /`, unary `-`, `==`,
  `tostring`, `..`.
- Engine objects are userdata holding an object id: a destroyed object raises "This object was destroyed", never
  touches freed memory. A property read returns a copy of a Vector3, so `transform.position.x = 1` changes the copy;
  assign the whole vector back.

Each script object has its own interpreter (`LuaScriptVM` owns one `okay::lua::State`); share data through engine
objects or `ScriptHost::globals`.
