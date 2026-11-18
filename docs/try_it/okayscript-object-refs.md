---
id: okayscript-object-refs
---
## OkayScript: object references, Rigidbody calls and Find (bug fixes, branch `claude2/okayscript-object-refs`, not merged or released yet)

Issue #888, Findings 108 to 111. Before: a bullet that hit the third "Enemy" destroyed the first one,
`other.GetComponent<Rigidbody>()` pushed the attacker instead of the target, most `Rigidbody` calls did nothing, and
`GameObject.Find` returned switched-off objects.

1. Open the editor (`Tools\OkayEngine.exe` or Open from the launcher), then **File > Open** and pick
   `examples\physics\script_object_refs.okayscene`.
2. You see three red boxes, all named **Enemy**, a blue ball above the **right** one, and a yellow **Spinner** box.
3. Press **Play** and open **View > Console**.
4. Expect: the ball falls on the right enemy and **that** enemy disappears (the left and middle ones stay). The
   Console shows `Ball hit Enemy at x = 3`. Before this fix the left enemy (x = -3) vanished instead.
5. Expect: the yellow Spinner floats (its script turns gravity off with `rb.useGravity = false`) and spins about its
   vertical axis (`rb.AddTorque(..., ForceMode.VelocityChange)`). The Console shows
   `Spinner: spin = 3 rad/s, Find(Hidden Enemy) found = false, FindIncludingInactive found = true`
   ("Hidden Enemy" is switched off in the Hierarchy).
6. Try your own: select the Ball, open its script and change `Destroy(other);` to
   `other.GetComponent<Renderer>().color = Color.green;`. Press Play: the hit enemy turns green, not the ball.

What changed for scripts: `other`, `gameObject`, `GameObject.Find(..)`, `FindWithTag(..)`,
`FindGameObjectsWithTag(..)` and what `Instantiate` / `spawn` return now carry the object's unique id. They still
print and compare as the name, and a quoted name (`Destroy("Enemy")`) still means the first object with that name.
Details: [scripting.md](scripting.md#object-references).

**Status.** Engine tests run on Linux (see the PR). The editor and the Windows build were not run here, so steps 4
to 6 are unverified on Windows; the same scene file is loaded and played headless by `test_okayscript_object_refs`.
