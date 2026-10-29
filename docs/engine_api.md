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

## UIColorPicker

A row of color swatches. The chosen swatch is the current color.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIColorPicker>().SetValue(index)` | `UIColorPicker.SetValue/1` | step | index (Number), Target (Object) | - | Select a swatch by index. The chip shows that color. |
| `GetComponent<UIColorPicker>().Value()` | `UIColorPicker.Value/0` | value | Target (Object) | Result (Number) | The selected swatch, or -1. |

## Crosshair

An aim reticle. SpreadRecover pulls an extra gap back to 0; 0 keeps the authored spread.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<Crosshair>().SetSpread(pixels)` | `Crosshair.SetSpread/1` | step | pixels (Number), Target (Object) | - | Set the extra gap around the ticks, in pixels. |
| `GetComponent<Crosshair>().SetSpreadRecover(pixelsPerSecond)` | `Crosshair.SetSpreadRecover/1` | step | pixelsPerSecond (Number), Target (Object) | - | How fast the extra gap eases back to 0. 0 leaves the spread where it is. |
| `GetComponent<Crosshair>().KickSpread(pixels)` | `Crosshair.KickSpread/1` | step | pixels (Number), Target (Object) | - | Push the ticks apart. They ease back while spread recover is above 0. |
| `GetComponent<Crosshair>().Spread()` | `Crosshair.Spread/0` | value | Target (Object) | Result (Number) | The extra gap around the ticks, in pixels. |

## UIRadialProgress

A ring or pie. Thickness 0 is a pie. Spin turns the arc into a loader.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIRadialProgress>().SetValue(value)` | `UIRadialProgress.SetValue/1` | step | value (Number), Target (Object) | - | Fill fraction from 0 to 1. |
| `GetComponent<UIRadialProgress>().SetThickness(pixels)` | `UIRadialProgress.SetThickness/1` | step | pixels (Number), Target (Object) | - | Ring width in pixels. 0 draws a pie. |
| `GetComponent<UIRadialProgress>().SetStartAngle(degrees)` | `UIRadialProgress.SetStartAngle/1` | step | degrees (Number), Target (Object) | - | Where the arc starts. 0 is 12 o'clock. |
| `GetComponent<UIRadialProgress>().SetClockwise(on)` | `UIRadialProgress.SetClockwise/1` | step | on (Bool), Target (Object) | - | Fill clockwise when on. |
| `GetComponent<UIRadialProgress>().SetSpin(on)` | `UIRadialProgress.SetSpin/1` | step | on (Bool), Target (Object) | - | Spin the arc as a loader. |
| `GetComponent<UIRadialProgress>().SetShowPercent(on)` | `UIRadialProgress.SetShowPercent/1` | step | on (Bool), Target (Object) | - | Draw the fill as a percent in the middle. |

## UISlotGrid

A grid of buttons. Selecting one fires on_change. It does not know about inventory.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UISlotGrid>().SetValue(index)` | `UISlotGrid.SetValue/1` | step | index (Number), Target (Object) | - | Select a slot by index. -1 clears the selection. |
| `GetComponent<UISlotGrid>().SetColumns(columns)` | `UISlotGrid.SetColumns/1` | step | columns (Number), Target (Object) | - | How many slots across (1 to 16). |
| `GetComponent<UISlotGrid>().SetRows(rows)` | `UISlotGrid.SetRows/1` | step | rows (Number), Target (Object) | - | How many slots down (1 to 16). |
| `GetComponent<UISlotGrid>().Value()` | `UISlotGrid.Value/0` | value | Target (Object) | Result (Number) | The selected slot, or -1. |

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

## UIToast

A stack of toast notifications. The object needs a UI Toast.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIToast>().Push(title, body, kind)` | `UIToast.Push/3` | step | title (Text), body (Text), kind (Number), Target (Object) | - | Show a toast. kind: 0 info, 1 success, 2 warning, 3 error. |
| `GetComponent<UIToast>().SetDuration(seconds)` | `UIToast.SetDuration/1` | step | seconds (Number), Target (Object) | - | How long a new toast stays up, in seconds. |
| `GetComponent<UIToast>().SetCorner(corner)` | `UIToast.SetCorner/1` | step | corner (Number), Target (Object) | - | Where the stack sits: 0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 top-center. |
| `GetComponent<UIToast>().SetMaxStack(count)` | `UIToast.SetMaxStack/1` | step | count (Number), Target (Object) | - | How many toasts stay on screen. The oldest one drops off past this. |
| `GetComponent<UIToast>().Count()` | `UIToast.Count/0` | value | Target (Object) | Result (Number) | How many toasts are on screen right now. |

## UIModal

A modal dialog. Style Card restyles the Card, Title, Message, Confirm and Cancel inside it.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIModal>().Open()` | `UIModal.Open/0` | step | Target (Object) | - | Show the dialog. |
| `GetComponent<UIModal>().Close()` | `UIModal.Close/0` | step | Target (Object) | - | Hide the dialog. Fires on_modal_close. |
| `GetComponent<UIModal>().SetStyleCard(on)` | `UIModal.SetStyleCard/1` | step | on (Bool), Target (Object) | - | Turn the card styling on or off. Off leaves the dialog as it was authored. |
| `GetComponent<UIModal>().SetCardRadius(radius)` | `UIModal.SetCardRadius/1` | step | radius (Number), Target (Object) | - | Corner radius of the card named Card, in pixels. |
| `GetComponent<UIModal>().SetTitle(text)` | `UIModal.SetTitle/1` | step | text (Text), Target (Object) | - | Title text. Empty leaves whatever the Title label already says. |
| `GetComponent<UIModal>().SetMessage(text)` | `UIModal.SetMessage/1` | step | text (Text), Target (Object) | - | Body text. Empty leaves whatever the Message label already says. |
| `GetComponent<UIModal>().SetConfirmText(text)` | `UIModal.SetConfirmText/1` | step | text (Text), Target (Object) | - | Confirm button label. Empty leaves the button's own label. |
| `GetComponent<UIModal>().SetCancelText(text)` | `UIModal.SetCancelText/1` | step | text (Text), Target (Object) | - | Cancel button label. Empty leaves the button's own label. |
| `GetComponent<UIModal>().IsOpen()` | `UIModal.IsOpen/0` | value | Target (Object) | Result (Bool) | True while the dialog is shown. |

## UITheme

A saved skin for panels, buttons, sliders and the rest of the UI kit. The object needs a UI Theme.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UITheme>().SetPreset(preset)` | `UITheme.SetPreset/1` | step | preset (Number), Target (Object) | - | Switch to a built-in look and bake it: 0 custom, 1 dark, 2 light, 3 sci-fi, 4 fantasy. |
| `GetComponent<UITheme>().Apply()` | `UITheme.Apply/0` | step | Target (Object) | - | Bake this skin onto this object and its children. Widgets keep their positions. |
| `GetComponent<UITheme>().ApplyTo(objectName)` | `UITheme.ApplyTo/1` | step | objectName (Text), Target (Object) | - | Bake this skin onto the named object and its children. |

## UISlider

A draggable value. The object needs a UI Slider. on_change fires when the value moves.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UISlider>().SetValue(value)` | `UISlider.SetValue/1` | step | value (Number), Target (Object) | - | Set the value (snaps to the step). Fires OnValueChanged when it changes. |
| `GetComponent<UISlider>().SetStep(step)` | `UISlider.SetStep/1` | step | step (Number), Target (Object) | - | Snap spacing. 0 keeps the slider continuous. |
| `GetComponent<UISlider>().SetFormat(format)` | `UISlider.SetFormat/1` | step | format (Text), Target (Object) | - | Value label. "{v}" is the number. Empty keeps two decimals. |
| `GetComponent<UISlider>().GetValue()` | `UISlider.GetValue/0` | value | Target (Object) | Result (Number) | The current value. |

## UIProgressBar

A fill bar (health, loading). The object needs a UI Progress Bar.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIProgressBar>().SetValue(value)` | `UIProgressBar.SetValue/1` | step | value (Number), Target (Object) | - | Fill amount, 0 to 1. A damage trail eases down when Trail is set. |
| `GetComponent<UIProgressBar>().SetSegments(count)` | `UIProgressBar.SetSegments/1` | step | count (Number), Target (Object) | - | Chunk count. 0 or 1 is one solid bar. |
| `GetComponent<UIProgressBar>().SetTrail(seconds)` | `UIProgressBar.SetTrail/1` | step | seconds (Number), Target (Object) | - | Seconds for the damage trail to catch a drop. 0 hides it. |
| `GetComponent<UIProgressBar>().SetTextFormat(format)` | `UIProgressBar.SetTextFormat/1` | step | format (Text), Target (Object) | - | Label on the bar. "{n}" is the percent, "{v}" is 0..1. |

## UIButton

A clickable button. The object needs a UI Button. on_click fires when it is pressed.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<UIButton>().SetEase(ease, seconds)` | `UIButton.SetEase/2` | step | ease (Number), seconds (Number), Target (Object) | - | Hover/press ease: 0 linear, 1 smooth, 2 ease-out, 3 ease-in, and how many seconds it takes. |
| `GetComponent<UIButton>().SetInteractable(on)` | `UIButton.SetInteractable/1` | step | on (Bool), Target (Object) | - | False greys the button out and ignores clicks. |
| `GetComponent<UIButton>().Press()` | `UIButton.Press/0` | step | Target (Object) | - | Click the button. Fires OnClick. |

## Input

Keyboard, mouse and gamepad.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Input.mousePosition` | `Input.mousePosition` | value | - | mousePosition (Vector3) | The cursor in screen pixels (x, y; z is 0). |
| `Input.gamepadConnected` | `Input.gamepadConnected` | value | - | gamepadConnected (Bool) | True while a controller is plugged in. |
| `Input.GetKey(key)` | `Input.GetKey/1` | value | key (Text) | Result (Bool) | True while the key is held. |
| `Input.GetKeyDown(key)` | `Input.GetKeyDown/1` | value | key (Text) | Result (Bool) | True on the frame the key is pressed. |
| `Input.GetKeyUp(key)` | `Input.GetKeyUp/1` | value | key (Text) | Result (Bool) | True on the frame the key is released. |
| `Input.GetAxis(axis)` | `Input.GetAxis/1` | value | axis (Text) | Result (Number) | "Horizontal" (A/D, left/right, stick) or "Vertical" (W/S, up/down, stick): -1 to 1. |
| `Input.GetMouseButton(button)` | `Input.GetMouseButton/1` | value | button (Number) | Result (Bool) | True while the mouse button is held (0 left, 1 right, 2 middle). |
| `Input.GetMouseButtonDown(button)` | `Input.GetMouseButtonDown/1` | value | button (Number) | Result (Bool) | True on the frame the mouse button is pressed. |
| `Input.GetMouseButtonUp(button)` | `Input.GetMouseButtonUp/1` | value | button (Number) | Result (Bool) | True on the frame the mouse button is released. |
| `Input.GetButton(action)` | `Input.GetButton/1` | value | action (Text) | Result (Bool) | True while the action is held ("Jump", "Fire"...), on keyboard, mouse or controller. |
| `Input.GetButtonDown(action)` | `Input.GetButtonDown/1` | value | action (Text) | Result (Bool) | True on the frame the action is pressed. |
| `Input.GetButtonUp(action)` | `Input.GetButtonUp/1` | value | action (Text) | Result (Bool) | True on the frame the action is released. |
| `Input.GetActionValue(action)` | `Input.GetActionValue/1` | value | action (Text) | Result (Number) | How far the action is pressed, 0 to 1 (a trigger can be part way). |
| `Input.GetActionVector(action)` | `Input.GetActionVector/1` | value | action (Text) | Result (Vector3) | A direction action ("Move", "Look") as x, y in -1..1 (y up; z is 0). |
| `Input.Rumble(low, high, seconds)` | `Input.Rumble/3` | step | low (Number), high (Number), seconds (Number) | - | Shake the controller: low (heavy) and high (light) motor strength 0 to 1, for some seconds. |
| `Input.RebindAction(action, binding)` | `Input.RebindAction/2` | step | action (Text), binding (Text) | Result (Bool) | Bind an action to "key:e", "mouse:right", "pad:rt"... for this player (saved). False if the binding isn't understood. |
| `Input.DefineAction(action, binding, binding2)` | `Input.DefineAction/3` | step | action (Text), binding (Text), binding2 (Text) | - | Add a game action (or change its defaults), e.g. ("Dash", "key:q", "pad:rs"). Use "" for an unused binding. |
| `Input.DefineDirection(action, binding, binding2)` | `Input.DefineDirection/3` | step | action (Text), binding (Text), binding2 (Text) | - | Add a direction action, e.g. ("Steer", "keys:i,k,j,l", "stick:right"). |

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

## Audio

The mixer: overall volume and each group's (Music, SFX, UI, Voice), as a settings menu sets them.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Audio.masterVolume` | `Audio.masterVolume` | value | - | masterVolume (Number) | Overall volume, 0 to 1. |
| `Audio.masterVolume = masterVolume` | `Audio.masterVolume=` | step | masterVolume (Number) | - | Overall volume, 0 to 1. |
| `Audio.muted` | `Audio.muted` | value | - | muted (Bool) | Silence everything. |
| `Audio.muted = muted` | `Audio.muted=` | step | muted (Bool) | - | Silence everything. |
| `Audio.SetGroupVolume(group, volume)` | `Audio.SetGroupVolume/2` | step | group (Text), volume (Number) | - | Set one group's volume, 0 to 1 ("Music", "SFX", "UI" or "Voice"). |
| `Audio.GetGroupVolume(group)` | `Audio.GetGroupVolume/1` | value | group (Text) | Result (Number) | One group's volume. |
| `Audio.MuteGroup(group, mute)` | `Audio.MuteGroup/2` | step | group (Text), mute (Bool) | - | Mute or unmute one group. |
| `Audio.SaveSettings()` | `Audio.SaveSettings/0` | step | - | - | Store the volumes in the player's prefs now (they are also saved when the game closes). |

## SaveGame

Whole-game saves in named slots: every object, the player, inventories, script variables.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `SaveGame.playTime` | `SaveGame.playTime` | value | - | playTime (Number) | Seconds played, counting the time in the save that was loaded. |
| `SaveGame.Save(slot, label)` | `SaveGame.Save/2` | step | slot (Text), label (Text) | Result (Bool) | Save the game to a slot ("1", "quick"...) with a label for the load menu. False if it couldn't. |
| `SaveGame.Load(slot)` | `SaveGame.Load/1` | step | slot (Text) | Result (Bool) | Load a slot at the end of this frame. False if there's no save there. |
| `SaveGame.Exists(slot)` | `SaveGame.Exists/1` | value | slot (Text) | Result (Bool) | True if the slot has a save. |
| `SaveGame.Delete(slot)` | `SaveGame.Delete/1` | step | slot (Text) | Result (Bool) | Delete the save in a slot. |
| `SaveGame.Label(slot)` | `SaveGame.Label/1` | value | slot (Text) | Result (Text) | The label a slot was saved with ("" if empty). |

## Localization

Text in the player's language from the project's string table (docs/localization.md). A script hears a change of language in OnLanguageChanged().

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Localization.Language` | `Localization.Language` | value | - | Language (Text) | The current language's code ("en", "fr"...). |
| `Localization.Languages` | `Localization.Languages` | value | - | Languages (List) | The codes of the string table's languages, in the table's order. |
| `Localization.Get(key)` | `Localization.Get/1` | value | key (Text) | Result (Text) | The key's text in the player's language (the default language's when it has none, else the key). |
| `Localization.Get(key, arg0)` | `Localization.Get/2` | value | key (Text), arg0 (Any) | Result (Text) | The key's text with {0} filled in; a number also picks the plural form. |
| `Localization.Get(key, arg0, arg1)` | `Localization.Get/3` | value | key (Text), arg0 (Any), arg1 (Any) | Result (Text) | The key's text with {0} and {1} filled in. |
| `Localization.Get(key, arg0, arg1, arg2)` | `Localization.Get/4` | value | key (Text), arg0 (Any), arg1 (Any), arg2 (Any) | Result (Text) | The key's text with {0}, {1} and {2} filled in. |
| `Localization.SetLanguage(code)` | `Localization.SetLanguage/1` | step | code (Text) | Result (Bool) | Show every localized text in a language of the string table ("fr") and remember it for the next run. False when the table doesn't have it. |
| `Localization.LanguageName(code)` | `Localization.LanguageName/1` | value | code (Text) | Result (Text) | A language's name for a menu ("Français" for "fr"). |

## Sequence

Cutscenes: play a .okayseq timeline through a Sequence Player in the scene.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `Sequence.isPlaying` | `Sequence.isPlaying` | value | - | isPlaying (Bool) | True while a sequence plays. |
| `Sequence.time` | `Sequence.time` | value | - | time (Number) | Seconds into the playing sequence (0 when none plays). |
| `Sequence.lastEvent` | `Sequence.lastEvent` | value | - | lastEvent (Text) | The event being delivered: read it in OnSequenceEvent(). |
| `Sequence.Play(name)` | `Sequence.Play/1` | step | name (Text) | Result (Bool) | Play the sequence named like this (a Sequence Player's file, or Assets/<name>.okayseq). False if there is none. |
| `Sequence.Stop()` | `Sequence.Stop/0` | step | - | - | Stop every playing sequence; the camera goes back to the game. |

## NavMeshAgent

Walks the object along the scene's navigation mesh (the grid when none is baked). The object needs a Nav Mesh Agent.

| In a script | Node | Kind | Inputs | Gives | What it does |
|---|---|---|---|---|---|
| `GetComponent<NavMeshAgent>().speed` | `NavMeshAgent.speed` | value | Target (Object) | speed (Number) | Top speed in units per second. |
| `GetComponent<NavMeshAgent>().speed = speed` | `NavMeshAgent.speed=` | step | speed (Number), Target (Object) | - | Top speed in units per second. |
| `GetComponent<NavMeshAgent>().stoppingDistance` | `NavMeshAgent.stoppingDistance` | value | Target (Object) | stoppingDistance (Number) | How close to the destination counts as there. |
| `GetComponent<NavMeshAgent>().stoppingDistance = stoppingDistance` | `NavMeshAgent.stoppingDistance=` | step | stoppingDistance (Number), Target (Object) | - | How close to the destination counts as there. |
| `GetComponent<NavMeshAgent>().isStopped` | `NavMeshAgent.isStopped` | value | Target (Object) | isStopped (Bool) | True after Stop, until Resume or a new destination. |
| `GetComponent<NavMeshAgent>().velocity` | `NavMeshAgent.velocity` | value | Target (Object) | velocity (Vector3) | How it is moving right now, in units per second. |
| `GetComponent<NavMeshAgent>().destination` | `NavMeshAgent.destination` | value | Target (Object) | destination (Vector3) | The point it was last sent to. |
| `GetComponent<NavMeshAgent>().SetDestination(point)` | `NavMeshAgent.SetDestination/1` | step | point (Vector3), Target (Object) | - | Plan a path to a world point and start walking there. |
| `GetComponent<NavMeshAgent>().Stop()` | `NavMeshAgent.Stop/0` | step | Target (Object) | - | Stop where it is (Resume continues to the same point). |
| `GetComponent<NavMeshAgent>().Resume()` | `NavMeshAgent.Resume/0` | step | Target (Object) | - | Carry on to the destination after Stop. |
| `GetComponent<NavMeshAgent>().HasPath()` | `NavMeshAgent.HasPath/0` | value | Target (Object) | Result (Bool) | True while it is walking to a destination. |
| `GetComponent<NavMeshAgent>().RemainingDistance()` | `NavMeshAgent.RemainingDistance/0` | value | Target (Object) | Result (Number) | Path length left to the destination (0 when there is none). |
| `GetComponent<NavMeshAgent>().Arrived()` | `NavMeshAgent.Arrived/0` | value | Target (Object) | Result (Bool) | True once the last destination was reached (until the next SetDestination). |

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
