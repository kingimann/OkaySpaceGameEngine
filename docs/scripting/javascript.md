# JavaScript scripts (okay::js)

Internal record of our own JavaScript engine and how `.js` scripts bind to the engine. Feature map row 13.9.
Part 1 of the owner's request ("our own JavaScript engine, like three.js on top of this engine"): JavaScript as a game
scripting language next to OkayScript, Lua and C#. Part 2, okay.js (`web/okayjs/`, feature map 18.12, `docs/scripting/okayjs.md`), is a browser library with the same script API: the same game script runs in both.
The engine's core (`Js*.cpp`) is its own library, `okay_js` (`engine/CMakeLists.txt`), linked by the engine (game scripts,
`JavaScriptVM.cpp`) and by our own web browser's page engine (`okay_browser`: page scripts and the DOM binding,
`engine/src/Browser/Script.cpp`, feature map 17.18.17): one copy, two users.

## Where it came from

Everything here was written from **ECMA-262 (the ECMAScript language specification)** and general compiler and
interpreter techniques. No code from V8, SpiderMonkey, JavaScriptCore, QuickJS, Duktape, Hermes, Boa, polyfills, test262 or any
other engine or library was used, ported or followed (and no other engine was run to compare results: the expected
values in the tests are worked out by hand from the specification), and their structure, names and error messages were not copied (CLAUDE.md rule
19a). The error texts are our own wording. Per file:

| File | Written from |
| --- | --- |
| `engine/include/okay/JavaScript/JavaScript.hpp` | 6.1 (language types: UTF-16 strings, Number as a double, Symbol, Object), 6.1.7 (property attributes), 6.2.6 (property descriptors); our own object layout (a dense element part for arrays, an ordered property list with a hash index for big objects) |
| `engine/src/JavaScript/JsInternal.hpp` | our own bytecode (a stack machine whose bottom slots are a function's variables) and internal objects |
| `engine/src/JavaScript/JsLexer.cpp` | ch. 12 (12.2 white space, 12.3 line terminators, 12.4 comments, 12.7 names, 12.8 punctuators, 12.9.3 numbers with separators, 12.9.4 strings and escapes, 12.9.6 templates) |
| `engine/src/JavaScript/JsParser.cpp` | ch. 13 (expressions; 13.15.5 destructuring assignment), 14 (statements), 15 (functions, arrows, methods, generators, async, classes; 15.7.1 the private-name early errors), 16 (scripts; 16.2.2 / 16.2.3 import and export declarations), 12.10 (automatic semicolon insertion); recursive descent with precedence climbing; arrows found by looking past the matching `)` |
| `engine/src/JavaScript/JsCompiler.cpp` | 9.1 (environment records, resolved at compile time), 10.2.11 (FunctionDeclarationInstantiation), 14.2 / 14.3 (blocks and declarations, the not-initialized state of let / const), 14.7.4.4 (CreatePerIterationEnvironment), 14.15 (try with completion kinds through finally), 15.7.14 (ClassDefinitionEvaluation, with its private environment), 7.3.31-7.3.34 (PrivateFieldAdd, PrivateMethodOrAccessorAdd, PrivateGet, PrivateSet), 16.2.1.6.4 (a module's bindings in cells its loader makes) |
| `engine/src/JavaScript/JsInterpreter.cpp` | 10.2 (function [[Call]] / [[Construct]], 10.2.1.1 PrepareForOrdinaryCall, derived constructors), 13.15.3 (ApplyStringOrNumericBinaryOperator), 7.2.13 (IsLessThan), 13.3.7 (super), 27.5.3 (GeneratorStart / Resume / ResumeAbrupt / Yield), 27.7.5 (AsyncFunctionStart, Await), 6.2.12 (Private Names) and 13.10.1 (`#x in obj`) |
| `engine/src/JavaScript/JsRuntime.cpp` | ch. 7 (7.1 type conversion with 7.1.4.1.1 StringToNumber, 7.2 testing and comparison, 7.3 operations on objects), 6.1.6.1.20 (Number::toString: the shortest digits that read back, our own search), 10.1 (ordinary object internal methods, 10.1.6.3 ValidateAndApplyPropertyDescriptor, 10.1.9.2 OrdinarySetWithOwnDescriptor, 10.1.11.1 OrdinaryOwnPropertyKeys), 10.4.2 (array exotic objects), 10.4.3 (string exotic objects), 10.4.6 (module namespace exotic objects), 20.5 (errors); the module loader from 16.2.1 (LoadRequestedModules, Link, Evaluate, GetExportedNames, ResolveExport, InitializeEnvironment, GetModuleNamespace) |
| `engine/src/JavaScript/JsLibObject.cpp` | 20.1 Object, 20.2 Function (call, apply, bind), 20.3 Boolean, 20.4 Symbol, 20.5 Error and the native errors, 28.1 Reflect, 19.2 global functions (parseInt, parseFloat, isNaN, isFinite); 10.5 proxy exotic objects (every internal method and its invariants), 10.1.6.2 IsCompatiblePropertyDescriptor, 28.2 Proxy and Proxy.revocable |
| `engine/src/JavaScript/JsLibArray.cpp` | 23.1 Array; sort is our own stable bottom-up merge sort over a rooted copy (23.1.3.30 SortIndexedProperties / SortCompare); 25.1 ArrayBuffer, 25.2 SharedArrayBuffer, 23.2 TypedArray objects, 10.4.5 integer-indexed exotic objects, 25.3 DataView, 25.4 Atomics, 25.1.3.17-18 RawBytesToNumeric / NumericToRawBytes |
| `engine/src/JavaScript/JsLibString.cpp` | 22.1 String (22.1.3.19.1 GetSubstitution for the `$` patterns); TextEncoder and TextDecoder from the WHATWG Encoding Standard (8.1, 8.2, the UTF-8 decoder and encoder of 9.1) |
| `engine/src/JavaScript/JsLibNumber.cpp` | 21.1 Number (toFixed / toPrecision / toExponential round the exact decimal value half up as 21.1.3.3-21.1.3.5 say), 21.3 Math; Math.random is our own xorshift64* generator |
| `engine/src/JavaScript/JsLibJson.cpp` | 25.5 JSON (ECMA-404 grammar, InternalizeJSONProperty, SerializeJSONProperty, QuoteJSONString); structuredClone from the HTML Standard 2.7 (StructuredSerializeInternal, StructuredDeserialize, StructuredSerializeWithTransfer) and 8.10 |
| `engine/src/JavaScript/JsRegExp.cpp` | 22.2 RegExp (the pattern grammar of 22.2.1, the flags of 22.2.3, 22.2.7.2 RegExpBuiltinExec) and the String methods that take one; our own backtracking machine with an explicit stack and the empty-loop check of 22.2.2.3.1; 12.9.5 for the literal (a `/` starts one where an expression may start, chosen from the previous token) |
| `engine/src/JavaScript/JsLibCollections.cpp` | 7.4 (iterator operations), 27.1 (iteration), 23.1.5 / 22.1.5 (array and string iterators), 24.1 Map, 24.2 Set, 27.5 generator objects, 27.2 Promise with the jobs of 27.2.2 |
| `engine/src/JavaScript/JavaScriptVM.cpp` | the engine's `IScriptVM`, the typed binding table (`engine/src/Script/EngineApi.cpp`) and the generated binding layer (`okay/Scripting/ScriptBindings.hpp`) |
| `engine/src/Scripting/ScriptLanguageGlue.cpp` | language-neutral helpers (handler names, engine-set names, type words, signatures, the deny rules for typed methods); Lua's private copies of these can move onto it (Lua is Claude 3's lane) |

Why bytecode and not a tree walker: generators and async functions must not use operating system threads (rule 21a).
A suspended function keeps its frame (slots, operands, try handlers) in its generator object and copies it back to
resume. The instruction counter for the run budget also has one natural place.

## Language

All code runs as strict mode code (as modules and classes do in the spec): assigning to an undeclared name is a
ReferenceError, `this` in a plain call is `undefined`, no `with`, no octal literals.

Supported: `let` / `const` / `var` (with the not-initialized state: reading a `let` before its declaration is a
ReferenceError), functions, arrow functions, closures (a fresh binding per loop iteration for captured `let`),
default and rest parameters, `arguments`, spread in calls, arrays and objects, destructuring (array, object, nested,
defaults, rest, in parameters, declarations, assignments, `for` heads and `catch`), template literals and tagged
templates (`String.raw`), classes with `extends`, `super(...)`, `super.x` (also inside arrows), `static` members,
`static { }` blocks, instance and static fields, `#private` fields, methods, getters and setters (instance and static,
`#x in obj`, the early errors for an undeclared or duplicate name), getters and setters, computed method keys,
`new.target`, `for` / `for-in` / `for-of` / `for await`, `while`, `do`, `switch`, `break` and `continue` with labels,
`try` / `catch` (with or without a binding) / `finally` (also around `return`, `break` and `continue`), `throw`, the
conditional operator, optional chaining (`a?.b`, `a?.[k]`, `f?.()`, `a?.#x`), `??` and the logical assignments `&&=`,
`||=`, `??=`, `typeof`, `instanceof` (with `Symbol.hasInstance`), `in`, `delete`, `void`, the comma operator, every
arithmetic, bitwise, shift and comparison operator with the spec's precedence, BigInt, generators (`function*`,
`yield`, `yield*`, `next` / `return` / `throw`), async functions, arrows and generators with `await`, numeric
separators, binary / octal / hex literals, regular expression literals (with lookbehind), `__proto__: value` in
object literals, `import()` and, in scripts, `export default <expression | class | function>` and
`export <declaration>` at the top level (the game-script form).

Modules (16.2), through `Runtime::RunModule` and a host loader (`Runtime::modules`: resolve a specifier to a key,
fetch the source for a key, optionally fill `import.meta`): `import` declarations (default, named, `* as ns`,
string names, side-effect only, `with { ... }` attributes read and ignored), `export` declarations, lists, `default`,
`export ... from`, `export *` and `export * as ns`, live bindings, cycles (with the not-initialized state), the
namespace object (10.4.6: sorted, read-only, `Symbol.toStringTag` "Module"), `import.meta` (`url` by default), top-level
`await`, and `import()` through the same loader (in a later job, as a page's would be). Link errors (a missing or
ambiguous export) are SyntaxErrors; a host refusal is a TypeError. Our choices: the host's resolve and fetch are
synchronous; modules run one after another in dependency order (a module waiting on a top-level await holds up the ones
after it, where the spec may run independent ones in between); a function a module exports is made when that module
starts to run, not when it is linked (in a cycle, a call before that sees undefined). Game scripts are not modules:
`JavaScriptVM` sets no loader, so an `import` declaration there is a SyntaxError and `import()` is rejected.

Not supported (yet): Unicode property escapes `\p{...}` and the `v` flag in regular expressions (they need Unicode
Character Database tables, which we do not ship: a clear SyntaxError; without the `u` flag `\p` is a plain `p`), the
`d` flag's indices (accepted, no `indices`), computed class field names, `super()` inside an arrow function, getters /
setters through `super` in compound assignments, labelled function declarations (an error in strict code, as the spec
says), `String.prototype.normalize` (returns the string), locale rules (`localeCompare` compares code units, case
mapping covers ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic), resizable / growable buffers
(`ArrayBuffer(n, { maxByteLength })` makes a fixed one; no `resize`), `Atomics.wait` (the only thread can not block:
a TypeError) and `waitAsync`, iterator helpers (`Iterator`), `Array.fromAsync`. `yield*` forwards `next` values and
`return` but not `throw` to the inner iterator, and an exception thrown inside a `for-of` body does not call the
iterator's `return` (a `break` or `return` does).

There is no `eval` and no `Function` constructor: game scripts can not compile code from strings (they throw an
EvalError).

## Library

`Object` (keys, values, entries, assign, create, defineProperty / defineProperties, getOwnPropertyNames / Symbols /
Descriptor(s), getPrototypeOf / setPrototypeOf, freeze / seal / preventExtensions and their tests, fromEntries, is,
hasOwn; prototype hasOwnProperty, isPrototypeOf, propertyIsEnumerable, toString with Symbol.toStringTag, valueOf),
`Function` (call, apply, bind, toString), `Array` (isArray, of, from; at, concat, copyWithin, entries, every, fill,
filter, find, findIndex, findLast, findLastIndex, flat, flatMap, forEach, includes, indexOf, join, keys, lastIndexOf,
map, pop, push, reduce, reduceRight, reverse, shift, slice, some, sort with a comparator, splice, toReversed, toSorted,
toString, unshift, values, Symbol.iterator), `String` (fromCharCode, fromCodePoint; at, charAt, charCodeAt,
codePointAt, concat, endsWith, includes, indexOf, lastIndexOf, localeCompare, padEnd, padStart, repeat, replace,
replaceAll, slice, split, startsWith, substr, substring, toLowerCase, toUpperCase, trim, trimStart, trimEnd, iterator),
`RegExp` (literals and the constructor, exec, test, toString, lastIndex, source, flags and the flag getters; flags
d g i m s u y; groups, named groups, backreferences, lookahead, greedy and lazy quantifiers, classes and the \d \w \s
escapes; String match, matchAll, replace and replaceAll with `$1` `$<name>` `$&` patterns or a function, search and
split with a RegExp), `Number` (constants, isFinite, isInteger, isNaN, isSafeInteger; toString(radix), toFixed,
toPrecision, toExponential),
`Boolean`, `Symbol` (for, keyFor, description, the well-known iterator, asyncIterator, toPrimitive, toStringTag,
hasInstance, species), `Math` (every function and constant of 21.3), `JSON` (parse with a reviver, stringify with a replacer
function or list, indent and toJSON), `Error`, `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`, `EvalError`,
`URIError`, `AggregateError` (message, cause, stack), `Map`, `Set` (insertion order, live iteration while entries are
added and deleted), iterators and generators, `Promise` (then, catch, finally, resolve, reject, all, allSettled, any,
race) with the job queue, `Reflect` (every function; `set` with a receiver), `Proxy` and `Proxy.revocable` (all 13
traps, every invariant of 10.5 checked, a revoked proxy is a TypeError), `ArrayBuffer` (slice, transfer,
transferToFixedLength, detached, isView), `SharedArrayBuffer` (a plain buffer: one thread), the eleven typed arrays
(`Int8Array` .. `Float64Array`, `Uint8ClampedArray`, `BigInt64Array`, `BigUint64Array`, with every method of 23.2.3,
`from`, `of`, species, the integer-indexed exotic rules and detach checks), `DataView` (every get / set, both byte
orders), `Atomics` (add, and, compareExchange, exchange, isLockFree, load, notify, or, store, sub, xor),
`TextEncoder` / `TextDecoder` (UTF-8 only; fatal, ignoreBOM, stream), `structuredClone` (with `transfer`), `WeakMap`,
`WeakSet`, `WeakRef`, `FinalizationRegistry`, `Date`, `BigInt`, `Intl` basics, `globalThis`, `parseInt`,
`parseFloat`, `isNaN`, `isFinite`, `queueMicrotask`, and `console.log` / `info` / `warn` / `error` / `debug` (to the
engine Console).

Binary data rules (our choices where the spec leaves them open): elements are stored little-endian; a buffer is at most
1 GiB and must fit in the heap limit, checked before any byte is allocated (a RangeError otherwise); every element
access checks the index against the buffer's bytes as they are now, so a view of a detached buffer (or a method whose
callback or `valueOf` detached it half way) reads `undefined` and writes nothing. structuredClone: a value that can
not be cloned (functions, symbols, proxies, promises, weak collections, engine objects, detached buffers, and a
`SharedArrayBuffer`, as the HTML standard says without cross-origin isolation) is an Error named `DataCloneError` (the
HTML standard's DOMException: the engine has none); deep nesting is cloned with an explicit stack.

## Sandbox, budget and memory

- No file, network, process or module access: the global object has none of `require`, `process`, `fetch`,
  `XMLHttpRequest`, `WebSocket`; game scripts get no module loader (`import` declarations are a SyntaxError and
  `import()` is rejected); `eval` and `Function` throw. Engine calls go through the same deny rules
  as Lua (`scriptbind::IsDeniedClass` / `IsDeniedMethod`), for the typed table and the generated layer alike.
- An instruction budget per callback (`JavaScriptVM::kDefaultBudget`, 20 million): every bytecode instruction counts,
  and library functions charge in proportion to their work (string building, join, split, repeat, pad, sort, JSON
  both ways, spread, Array.from, flat, iteration steps, every step of the regular expression machine, a thrown
  exception and a new error's stack text), so catastrophic backtracking (`/(a+)+$/` on a long string) stops too. When it runs
  out the script stops with "the script ran past its instruction budget"; `try` / `catch` can not catch the stop. The
  next callback gets a fresh budget.
- A heap limit (`Runtime::memoryLimit`, 256 MB by default, `JavaScriptVM::SetMemoryLimit`): "the script used more
  memory than allowed". Strings are at most 2^28 characters; arrays made by library calls at most 2^26 elements.
- Recursion is bounded: 2400 frames of script calls, 120 nested library-to-script calls, a parser nesting limit (the
  parser and compiler then fit in a 256 KB stack), and depth limits in JSON (256), `flat` (200) and `join` (100, with
  cycles joining as ""). Proxies inside proxies (a chain, or a proxy on a prototype chain of proxies) stop at 200 with
  a RangeError, and every proxy step charges the budget; a trap that calls back into its own proxy stops at the
  library-call limit. Modules: loading and linking are loops (a 2000-module import chain is fine), export searches
  stop at 1000 re-export levels with a SyntaxError, at most 20000 modules, each charged to the budget.
  structuredClone uses an explicit stack (100000-deep nesting clones).
- The hostile one-liners in `js_core` (endless loops with and without try, exponential string growth, huge repeat /
  pad / join / Array.from, 5 million element sorts, recursion through library calls, promise and await loops,
  generators that never end, getters and valueOf that recurse, 600 nested parentheses, prototype accessors on
  Array.prototype, `class extends null`) each stop within the budget with a clear error, under 10 seconds even under
  AddressSanitizer.

## The collector and rooting

Mark and sweep (`Runtime::Collect`) with an explicit gray stack (no recursion). It runs only at safe points of the
bytecode loop (backward jumps and calls), never inside an allocation, so library code only has to keep what it holds
across a call back into JavaScript. The roots: the value stack, the frames, the globals and the global lexical
declarations, the intrinsics, the host refs, the job queue, the scratch list, and registered vectors. Everything a
library call allocates or interns goes into the scratch list until that call returns (a nested loop's safe points only
drop scratch entries made inside that loop), and loops that call back drop each step's entries
(`Runtime::ScratchScope`). Values a library call reads out of storage and uses after calling back are kept with
`Runtime::Keep` or a `Runtime::RootedVector` (sort's copy of the elements, apply's argument list). This is the class
of bug the Lua review found in table.sort; `js_core` runs the conformance script and a rooting script (a sort
comparator that allocates and shrinks the array, map / filter / reduce, a JSON reviver, Map.forEach that deletes and
adds, getters, spread of generators) with a collection at every safe point, under ASan and UBSan, and checks the
results match the normal run.

## Game scripts

A script exports its behaviour. `export default class` is the usual form; its instance fields are the Inspector
settings (numbers, strings, booleans and Vector3), and the engine calls the lifecycle methods by name: `Awake`,
`Start`, `Update(dt)`, `LateUpdate`, `FixedUpdate(dt)`, `OnEnable`, `OnDisable`, `OnDestroy`, `OnTriggerEnter(other)`,
`OnTriggerStay`, `OnTriggerExit`, `OnCollisionEnter(other)`, `OnCollisionStay`, `OnCollisionExit`, and any other event
the engine fires (`on_sequence_event` reaches `OnSequenceEvent`). `export default { ... }` works the same, and a script
with no export uses its global functions (`function Update(dt) {}`, the old `function update(dt) {}` too) and its
top-level variables. `this.gameObject` and `this.transform` are set before the first callback (also globals
`gameObject` / `transform`). Promise jobs run at the end of each callback.

Engine objects are host objects that hold an object id (and which component), so a destroyed object is an error
("This object was destroyed"), never freed memory. The same engine object always gives the same JavaScript object, so
`===` compares them. Bindings, in order (the same two tables Lua uses, no second binding system):

1. The typed binding table (Script/EngineApi): `GameObject`, `Transform`, `Vector3`, `Input`, `Time`, `Debug`,
   `Mathf`, ... Static types are global objects whose static properties are accessors and whose static methods are
   functions (`Input.GetKey("d")`, `Time.deltaTime`, `GameObject.Find("Enemy")`). Instance members are properties and
   methods (`this.transform.position`, `this.transform.Translate(0, 1, 0)`). A wrong call is a TypeError naming the
   expected signature.
2. The generated binding layer (Scripting/ScriptBindings) for every other component and for members the typed table
   lacks: `GetComponent("Light").intensity = 2`, `GetComponent("Rigidbody").InvMass()`.

`GetComponent(name)` (global, or `obj.GetComponent(name)`) gives the component or `null`; `Destroy(obj)` removes an
object. `Vector3` is a constructor (`new Vector3(1, 2, 3)`, also callable without `new`) with the static members of the
typed table (`Vector3.up`, `Vector3.Distance(a, b)`), and vectors have `x`, `y`, `z`, the typed table's members
(`magnitude`, `normalized`, ...) and JavaScript helpers (JavaScript has no operator overloading): `add`, `sub`,
`scale` (a number or a vector), `div`, `negate`, `equals`, `clone`, `set`, `toString`. Setting an unknown or read-only
property on an engine object is a TypeError (no silent typos); reading an unknown one gives `undefined`, as in
JavaScript.

## Editor

The Script Editor gets JavaScript colours, hover docs and completion words (`oked::JavaScriptLanguageWords`,
`LanguageWordsFor`), member completion after `.` (`JavaScriptVM::CompletionMembers`), errors with line and column and
warnings for engine names that do not exist and misspelled handlers (`JavaScriptVM::ValidateAll`, `Lint`), Format
Document by brackets (`JavaScriptVM::Format`), the Inspector's settings from the exported class
(`JavaScriptVM::Settings`), New Script with "JavaScript (.js)" and the starter script (`StarterScriptSource("javascript")`).
`.js` files open, list and attach as JavaScript (`ScriptLanguageForExtension`).

## Speed

Simple loops against our Lua on the same machine (`-O2`, one run each, wall time; scripts in the PR record):

| Loop | JavaScript | Lua |
| --- | --- | --- |
| 10 million iterations of `s += (i * 2) % 7` | 632 ms | 583 ms |
| recursive `fib(27)` | 51 ms | 50 ms |
| 200 000 objects made, then 10 passes summing three fields | 315 ms | 443 ms |
| 200 000 strings joined and split again | 295 ms | 194 ms |
| sorting 200 000 numbers with a comparator | 319 ms | 427 ms |
| 3 million calls of a closure that updates a captured variable | 296 ms | 406 ms |

## Tests

- `js_core` (standalone: only `engine/src/JavaScript/Js*.cpp`): spec behaviours (precedence, conversions,
  closures and per-iteration bindings, prototypes and classes, the library, exceptions with stack text and lines,
  generators, promises and async), typed arrays and binary data (with detaching in the middle of methods), proxies
  (every trap, the invariants, revocation, hostile chains), private members, modules (an in-memory host: live
  bindings, cycles, top-level await, import.meta, dynamic import, link errors, long chains, a mutated-graph fuzz),
  structuredClone, syntax errors with line and column, the conformance script
  `tests/javascript/conformance.js` (checks written from the spec section by section, also run with a collection at
  every safe point), 40 hostile one-liners, the memory limit, the rooting script, the host API, and a mutation fuzz
  (`OKAY_JS_FUZZ_ITERS`). Clean under ASan and UBSan.
- `js_script`: game scripts on real GameObjects (lifecycle, settings and Inspector overrides, input and time, events
  with the other object, GetComponent and identity, errors with lines, destroyed objects, the budget, global-function
  scripts, syntax errors, bad engine calls, generators and async in scripts, the scene round trip, editor support,
  Format, the generated binding layer, the sandbox, the deny rules, inheritance and contact globals, garbage per frame).
