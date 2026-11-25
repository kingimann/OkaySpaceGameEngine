# scripting/: Scripting

> **What this is:** the list of pages in `docs/scripting/`, one line each. Scripting: OkayScript and Visual Script. The generated node lists stay at the top of docs/ (see the start page).
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [One table of engine functions](script_api_registry.md): The shared table every script language reads (OkayScript, C#, visual-script nodes); how to add a function once.
- [OkayScript](scripting.md): The OkayScript language guide and its function reference, in one page.
- [Lua scripts (okay::lua)](lua.md): Our own Lua 5.4 interpreter for `.lua` scripts: what it supports, how scripts bind to the engine, and what each file was written from.
- [JavaScript scripts (okay::js)](javascript.md): Our own JavaScript engine (ECMA-262) for `.js` scripts: what it supports and does not, the sandbox and budget, how scripts bind to the engine, and what each file was written from.
- [OkayScript bytecode machine](okayscript_vm.md): Design note for the compiler and stack machine that run OkayScript.
- [Visual Script clarity](visual_script_clarity.md): How visual-script nodes explain themselves, and the audit behind it.
- [OkayVS — the visual scripting text format](visual_scripting.md): A visual script is a graph of **nodes** wired by **execution** links (control flow) and **data** links (values).
