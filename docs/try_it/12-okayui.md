---
id: 12
group: switches
row: | 12 | OkayUI instead of Dear ImGui (off by default) | `OKAY_UI_BACKEND=okayui` | Unverified, only a few screens |
---
### 12. OkayUI

`OKAY_UI_BACKEND=okayui` (or `OKAY_OWN_STACK=1`) starts the launcher and editor on OkayUI where it is compiled in. Only
the screens that have an OkayUI port change (in the editor, the Console panel today); everything else stays Dear ImGui.
The About popup shows `ui=okayui` when it took effect. Whether the Windows release contains OkayUI is unverified: if not,
the line stays `ui=imgui`. Nothing was run in a real window.
