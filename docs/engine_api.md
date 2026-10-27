# Engine API reference

Generated from the engine's binding table (`engine/src/Script/EngineApi.cpp`): the same bindings
typed scripts (`okayscript-next`, `class Name : Behaviour`) call and the visual-script
**Engine API** nodes run. Don't edit this page by hand;
after changing the table, regenerate it with `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types`
(run from the build folder). The `vs_types` test fails while it's out of date.

In a graph, members of GameObject, Transform and Rigidbody act on the graph's own object, or on
whatever is wired into (or typed into) their last input, **Target**. Steps run in the flow;
value nodes give a value whenever something reads them.

## GameObject

An object in the scene.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `gameObject.name` | `GameObject.name` | value | Target (Object) | name (Text) | The object's name (as shown in the Hierarchy). |
| `gameObject.name = name` | `GameObject.name=` | step | name (Text), Target (Object) | - | The object's name (as shown in the Hierarchy). |
| `gameObject.tag` | `GameObject.tag` | value | Target (Object) | tag (Text) | The object's tag. |
| `gameObject.tag = tag` | `GameObject.tag=` | step | tag (Text), Target (Object) | - | The object's tag. |
| `gameObject.active` | `GameObject.active` | value | Target (Object) | active (Bool) | Whether the object is active. Inactive objects don't update or draw. |
| `gameObject.active = active` | `GameObject.active=` | step | active (Bool), Target (Object) | - | Whether the object is active. Inactive objects don't update or draw. |
| `gameObject.transform` | `GameObject.transform` | value | Target (Object) | transform (Object) | The object's Transform (position, rotation, scale). |
| `GameObject.Find(name)` | `GameObject.Find/1` | value | name (Text) | Result (Object) | The first active object with this name, or null if there is none. |
| `GameObject.FindWithTag(tag)` | `GameObject.FindWithTag/1` | value | tag (Text) | Result (Object) | The first active object with this tag, or null if there is none. |

## Transform

Position, rotation and scale of an object.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `transform.position` | `Transform.position` | value | Target (Object) | position (Vector3) | Position in the world. |
| `transform.position = position` | `Transform.position=` | step | position (Vector3), Target (Object) | - | Position in the world. |
| `transform.localPosition` | `Transform.localPosition` | value | Target (Object) | localPosition (Vector3) | Position relative to the parent. |
| `transform.localPosition = localPosition` | `Transform.localPosition=` | step | localPosition (Vector3), Target (Object) | - | Position relative to the parent. |
| `transform.eulerAngles` | `Transform.eulerAngles` | value | Target (Object) | eulerAngles (Vector3) | Rotation in the world, as X / Y / Z angles in degrees. |
| `transform.eulerAngles = eulerAngles` | `Transform.eulerAngles=` | step | eulerAngles (Vector3), Target (Object) | - | Rotation in the world, as X / Y / Z angles in degrees. |
| `transform.localEulerAngles` | `Transform.localEulerAngles` | value | Target (Object) | localEulerAngles (Vector3) | Rotation relative to the parent, in degrees. |
| `transform.localEulerAngles = localEulerAngles` | `Transform.localEulerAngles=` | step | localEulerAngles (Vector3), Target (Object) | - | Rotation relative to the parent, in degrees. |
| `transform.localScale` | `Transform.localScale` | value | Target (Object) | localScale (Vector3) | Scale relative to the parent. |
| `transform.localScale = localScale` | `Transform.localScale=` | step | localScale (Vector3), Target (Object) | - | Scale relative to the parent. |
| `transform.forward` | `Transform.forward` | value | Target (Object) | forward (Vector3) | The direction the object faces (its +Z), in the world. |
| `transform.right` | `Transform.right` | value | Target (Object) | right (Vector3) | The object's right (+X), in the world. |
| `transform.up` | `Transform.up` | value | Target (Object) | up (Vector3) | The object's up (+Y), in the world. |
| `transform.gameObject` | `Transform.gameObject` | value | Target (Object) | gameObject (Object) | The object this Transform belongs to. |
| `transform.Translate(offset)` | `Transform.Translate/1` | step | offset (Vector3), Target (Object) | - | Move by an offset (relative to the parent). |
| `transform.Translate(x, y, z)` | `Transform.Translate/3` | step | x (Number), y (Number), z (Number), Target (Object) | - | Move by x, y, z (relative to the parent). |
| `transform.Rotate(degrees)` | `Transform.Rotate/1` | step | degrees (Vector3), Target (Object) | - | Turn by X / Y / Z degrees around the object's own axes. |
| `transform.Rotate(x, y, z)` | `Transform.Rotate/3` | step | x (Number), y (Number), z (Number), Target (Object) | - | Turn by x, y, z degrees around the object's own axes. |
| `transform.LookAt(target)` | `Transform.LookAt/1` | step | target (Vector3), Target (Object) | - | Turn so forward points at a world position. |

## Vector3

A point or direction: x, y, z.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `vector.magnitude` | `Vector3.magnitude` | value | Vector3 (Vector3) | magnitude (Number) | The length. |
| `vector.normalized` | `Vector3.normalized` | value | Vector3 (Vector3) | normalized (Vector3) | The same direction with length 1. |
| `Vector3.zero` | `Vector3.zero` | value | - | zero (Vector3) | (0, 0, 0) |
| `Vector3.one` | `Vector3.one` | value | - | one (Vector3) | (1, 1, 1) |
| `Vector3.up` | `Vector3.up` | value | - | up (Vector3) | (0, 1, 0) |
| `Vector3.down` | `Vector3.down` | value | - | down (Vector3) | (0, -1, 0) |
| `Vector3.right` | `Vector3.right` | value | - | right (Vector3) | (1, 0, 0) |
| `Vector3.left` | `Vector3.left` | value | - | left (Vector3) | (-1, 0, 0) |
| `Vector3.forward` | `Vector3.forward` | value | - | forward (Vector3) | (0, 0, 1) |
| `Vector3.back` | `Vector3.back` | value | - | back (Vector3) | (0, 0, -1) |
| `Vector3.Distance(a, b)` | `Vector3.Distance/2` | value | a (Vector3), b (Vector3) | Result (Number) | Distance between two points. |
| `Vector3.Dot(a, b)` | `Vector3.Dot/2` | value | a (Vector3), b (Vector3) | Result (Number) | Dot product. |
| `Vector3.Cross(a, b)` | `Vector3.Cross/2` | value | a (Vector3), b (Vector3) | Result (Vector3) | Cross product. |
| `Vector3.Lerp(a, b, t)` | `Vector3.Lerp/3` | value | a (Vector3), b (Vector3), t (Number) | Result (Vector3) | Blend from a (t = 0) to b (t = 1). |
| `Vector3.MoveTowards(current, target, maxDistance)` | `Vector3.MoveTowards/3` | value | current (Vector3), target (Vector3), maxDistance (Number) | Result (Vector3) | Step from current toward target, moving at most maxDistance. |

## Rigidbody

A physics body: velocity, mass and forces. The object needs a Rigidbody component.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<Rigidbody>().velocity` | `Rigidbody.velocity` | value | Target (Object) | velocity (Vector3) | Speed and direction in units per second. |
| `GetComponent<Rigidbody>().velocity = velocity` | `Rigidbody.velocity=` | step | velocity (Vector3), Target (Object) | - | Speed and direction in units per second. |
| `GetComponent<Rigidbody>().angularVelocity` | `Rigidbody.angularVelocity` | value | Target (Object) | angularVelocity (Vector3) | Spin around X / Y / Z in radians per second. |
| `GetComponent<Rigidbody>().angularVelocity = angularVelocity` | `Rigidbody.angularVelocity=` | step | angularVelocity (Vector3), Target (Object) | - | Spin around X / Y / Z in radians per second. |
| `GetComponent<Rigidbody>().mass` | `Rigidbody.mass` | value | Target (Object) | mass (Number) | How heavy the body is (more than 0). |
| `GetComponent<Rigidbody>().mass = mass` | `Rigidbody.mass=` | step | mass (Number), Target (Object) | - | How heavy the body is (more than 0). |
| `GetComponent<Rigidbody>().gravityScale` | `Rigidbody.gravityScale` | value | Target (Object) | gravityScale (Number) | How strongly gravity pulls it: 1 normal, 0 none. |
| `GetComponent<Rigidbody>().gravityScale = gravityScale` | `Rigidbody.gravityScale=` | step | gravityScale (Number), Target (Object) | - | How strongly gravity pulls it: 1 normal, 0 none. |
| `GetComponent<Rigidbody>().drag` | `Rigidbody.drag` | value | Target (Object) | drag (Number) | How quickly it slows down by itself (0 = never). |
| `GetComponent<Rigidbody>().drag = drag` | `Rigidbody.drag=` | step | drag (Number), Target (Object) | - | How quickly it slows down by itself (0 = never). |
| `GetComponent<Rigidbody>().isKinematic` | `Rigidbody.isKinematic` | value | Target (Object) | isKinematic (Bool) | When true, forces and gravity don't move it; only scripts do. |
| `GetComponent<Rigidbody>().isKinematic = isKinematic` | `Rigidbody.isKinematic=` | step | isKinematic (Bool), Target (Object) | - | When true, forces and gravity don't move it; only scripts do. |
| `GetComponent<Rigidbody>().AddForce(force)` | `Rigidbody.AddForce/1` | step | force (Vector3), Target (Object) | - | Push it, over the next physics step (heavier bodies move less). |
| `GetComponent<Rigidbody>().AddImpulse(impulse)` | `Rigidbody.AddImpulse/1` | step | impulse (Vector3), Target (Object) | - | Kick it at once (a jump, a hit). |
| `GetComponent<Rigidbody>().AddTorque(torque)` | `Rigidbody.AddTorque/1` | step | torque (Vector3), Target (Object) | - | Twist it around X / Y / Z. |
| `GetComponent<Rigidbody>().AddForceAtPosition(force, point)` | `Rigidbody.AddForceAtPosition/2` | step | force (Vector3), point (Vector3), Target (Object) | - | Push it at a world point (off-centre pushes also spin it). |

## PlayerView

Switches one player between first person and third person. The object needs a Player View.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<PlayerView>().SetView(view)` | `PlayerView.SetView/1` | step | view (Number), Target (Object) | - | Ease to a view: 0 first person (camera at the eyes), 1 third person (over the shoulder). |
| `GetComponent<PlayerView>().GetView()` | `PlayerView.GetView/0` | value | Target (Object) | Result (Number) | The view asked for: 0 first person, 1 third person. The camera may still be on its way there. |

## UIDropdown

A dropdown. Search, a row cap and per-option glyphs are optional.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIDropdown>().SetSearchable(on)` | `UIDropdown.SetSearchable/1` | step | on (Bool), Target (Object) | - | Show a search row that filters the options while the list is open. |
| `GetComponent<UIDropdown>().SetMaxVisible(rows)` | `UIDropdown.SetMaxVisible/1` | step | rows (Number), Target (Object) | - | How many matching options the open list shows. 0 shows every match. |
| `GetComponent<UIDropdown>().SetOptionIcon(index, glyph)` | `UIDropdown.SetOptionIcon/2` | step | index (Number), glyph (Text), Target (Object) | - | A short glyph drawn in front of one option. |
| `GetComponent<UIDropdown>().Searchable()` | `UIDropdown.Searchable/0` | value | Target (Object) | Result (Bool) | True when the open list has a search row. |

## UIScrollView

A scroll view. Inertia, elastic edges and the bar style are optional.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIScrollView>().SetInertia(rate)` | `UIScrollView.SetInertia/1` | step | rate (Number), Target (Object) | - | Wheel glide. 0 scrolls one notch immediately. Higher stops the glide sooner. |
| `GetComponent<UIScrollView>().SetElastic(on)` | `UIScrollView.SetElastic/1` | step | on (Bool), Target (Object) | - | Let the content pass the ends, then spring back. |
| `GetComponent<UIScrollView>().SetBarStyle(style)` | `UIScrollView.SetBarStyle/1` | step | style (Number), Target (Object) | - | Scrollbar: 0 the usual bar, 1 thin, 2 hidden until the view is scrolling. |

## UITabs

A tab bar. Closable tabs fire on_tab_close. Vertical is a style.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UITabs>().SetClosable(on)` | `UITabs.SetClosable/1` | step | on (Bool), Target (Object) | - | Draw an X on each tab. Clicking it removes that tab. |
| `GetComponent<UITabs>().CloseTab(index)` | `UITabs.CloseTab/1` | step | index (Number), Target (Object) | - | Remove one tab and fire on_tab_close. |
| `GetComponent<UITabs>().Count()` | `UITabs.Count/0` | value | Target (Object) | Result (Number) | How many tabs are left. |

## UITooltip

A hover tooltip. It already waits and follows the cursor. Rich text and the offset are optional.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UITooltip>().SetRichText(on)` | `UITooltip.SetRichText/1` | step | on (Bool), Target (Object) | - | Draw <b> and #rrggbb in the tooltip text. |
| `GetComponent<UITooltip>().SetOffset(x, y)` | `UITooltip.SetOffset/2` | step | x (Number), y (Number), Target (Object) | - | Pixels from the cursor to the box. 14, 14 is the usual spot. |
| `GetComponent<UITooltip>().SetDelay(seconds)` | `UITooltip.SetDelay/1` | step | seconds (Number), Target (Object) | - | How long the pointer must hover before the tooltip shows. |

## UIInputField

A text field. Password, a character limit and integer or decimal mode already exist. Multiline is optional.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIInputField>().SetMultiline(on)` | `UIInputField.SetMultiline/1` | step | on (Bool), Target (Object) | - | Enter inserts a newline instead of submitting. |
| `GetComponent<UIInputField>().SetMaxLength(length)` | `UIInputField.SetMaxLength/1` | step | length (Number), Target (Object) | - | The most characters the field accepts. |
| `GetComponent<UIInputField>().SetContentType(type)` | `UIInputField.SetContentType/1` | step | type (Number), Target (Object) | - | 0 standard, 1 integer, 2 decimal, 3 password. |

## Input

Keyboard, mouse and gamepad.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Input.mousePosition` | `Input.mousePosition` | value | - | mousePosition (Vector3) | The cursor in screen pixels (x, y; z is 0). |
| `Input.GetKey(key)` | `Input.GetKey/1` | value | key (Text) | Result (Bool) | True while the key is held. |
| `Input.GetKeyDown(key)` | `Input.GetKeyDown/1` | value | key (Text) | Result (Bool) | True on the frame the key is pressed. |
| `Input.GetKeyUp(key)` | `Input.GetKeyUp/1` | value | key (Text) | Result (Bool) | True on the frame the key is released. |
| `Input.GetAxis(axis)` | `Input.GetAxis/1` | value | axis (Text) | Result (Number) | "Horizontal" (A/D, left/right, stick) or "Vertical" (W/S, up/down, stick): -1 to 1. |
| `Input.GetMouseButton(button)` | `Input.GetMouseButton/1` | value | button (Number) | Result (Bool) | True while the mouse button is held (0 left, 1 right, 2 middle). |
| `Input.GetMouseButtonDown(button)` | `Input.GetMouseButtonDown/1` | value | button (Number) | Result (Bool) | True on the frame the mouse button is pressed. |
| `Input.GetMouseButtonUp(button)` | `Input.GetMouseButtonUp/1` | value | button (Number) | Result (Bool) | True on the frame the mouse button is released. |

## Time

Frame timing.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Time.deltaTime` | `Time.deltaTime` | value | - | deltaTime (Number) | Seconds since the last frame (inside FixedUpdate: the fixed step). |
| `Time.fixedDeltaTime` | `Time.fixedDeltaTime` | value | - | fixedDeltaTime (Number) | The FixedUpdate step in seconds. |
| `Time.time` | `Time.time` | value | - | time (Number) | Seconds since the game started. |
| `Time.frameCount` | `Time.frameCount` | value | - | frameCount (Number) | Frames since the game started. |
| `Time.timeScale` | `Time.timeScale` | value | - | timeScale (Number) | How fast time passes: 1 normal, 0.5 slow motion, 0 paused. |
| `Time.timeScale = timeScale` | `Time.timeScale=` | step | timeScale (Number) | - | How fast time passes: 1 normal, 0.5 slow motion, 0 paused. |

## Debug

Messages to the Console.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Debug.Log(message)` | `Debug.Log/1` | step | message (Any) | - | Print a message to the Console. |
| `Debug.LogWarning(message)` | `Debug.LogWarning/1` | step | message (Any) | - | Print a warning to the Console. |
| `Debug.LogError(message)` | `Debug.LogError/1` | step | message (Any) | - | Print an error to the Console. |

## Mathf

Maths helpers.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Mathf.PI` | `Mathf.PI` | value | - | PI (Number) | 3.14159... |
| `Mathf.Abs(value)` | `Mathf.Abs/1` | value | value (Number) | Result (Number) | The value without its sign. |
| `Mathf.Sqrt(value)` | `Mathf.Sqrt/1` | value | value (Number) | Result (Number) | Square root. |
| `Mathf.Sin(value)` | `Mathf.Sin/1` | value | value (Number) | Result (Number) | Sine (radians). |
| `Mathf.Cos(value)` | `Mathf.Cos/1` | value | value (Number) | Result (Number) | Cosine (radians). |
| `Mathf.Tan(value)` | `Mathf.Tan/1` | value | value (Number) | Result (Number) | Tangent (radians). |
| `Mathf.Floor(value)` | `Mathf.Floor/1` | value | value (Number) | Result (Number) | Round down. |
| `Mathf.Ceil(value)` | `Mathf.Ceil/1` | value | value (Number) | Result (Number) | Round up. |
| `Mathf.Round(value)` | `Mathf.Round/1` | value | value (Number) | Result (Number) | Round to the nearest whole number. |
| `Mathf.Sign(value)` | `Mathf.Sign/1` | value | value (Number) | Result (Number) | -1 for negative values, else 1. |
| `Mathf.Clamp01(value)` | `Mathf.Clamp01/1` | value | value (Number) | Result (Number) | Keep between 0 and 1. |
| `Mathf.Min(a, b)` | `Mathf.Min/2` | value | a (Number), b (Number) | Result (Number) | The smaller value. |
| `Mathf.Max(a, b)` | `Mathf.Max/2` | value | a (Number), b (Number) | Result (Number) | The larger value. |
| `Mathf.Pow(value, power)` | `Mathf.Pow/2` | value | value (Number), power (Number) | Result (Number) | value to the power. |
| `Mathf.Atan2(y, x)` | `Mathf.Atan2/2` | value | y (Number), x (Number) | Result (Number) | Angle of (x, y) in radians. |
| `Mathf.Clamp(value, min, max)` | `Mathf.Clamp/3` | value | value (Number), min (Number), max (Number) | Result (Number) | Keep between min and max. |
| `Mathf.Lerp(a, b, t)` | `Mathf.Lerp/3` | value | a (Number), b (Number), t (Number) | Result (Number) | Blend from a (t = 0) to b (t = 1). |
| `Mathf.MoveTowards(current, target, maxDelta)` | `Mathf.MoveTowards/3` | value | current (Number), target (Number), maxDelta (Number) | Result (Number) | Step from current toward target by at most maxDelta. |
