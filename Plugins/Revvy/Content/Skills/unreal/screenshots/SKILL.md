---
name: screenshots
display_name: Screenshot & Vision
description: Capture screenshots of the editor window, viewports, and blueprints for AI vision analysis
revvy_classes:
  - ScreenshotService
unreal_classes:
  - AutomationLibrary
keywords:
  - screenshot
  - capture
  - image
  - vision
---

# Screenshot & Vision Skill

## Methods

| Method | Use Case |
|--------|----------|
| `capture_from_pose(location, rotation, path, fov=0, show_ui=False, use_current_camera=False)` | **DEFAULT for 3D scene shots.** Synchronous, cross-platform, captures the level viewport from ANY camera pose **without disturbing the user's camera** (auto save/restore). `use_current_camera=True` = shoot exactly what the user sees. Returns JSON with `file_path` + `camera` block. |
| `describe_scene(location, rotation, fov=0, class_filter="", max_actors=50, only_in_frustum=True, use_current_camera=True)` | **The "eyes" JSON.** Actors visible from a pose: name/label/class/pos_cm/extent_cm/dist_cm/screen_px, nearest-first. Same `camera` block as capture — pair them at the same pose to correlate names ↔ pixels. |
| `capture_editor_window(path)` | Editor **UI** capture (whole window incl. Blueprint/Material graph tabs). Windows-only GDI. Use for UI, not for 3D scene checks. |
| `capture_asset_editor(asset_path, path)` | Open + focus + capture a specific asset's editor. Windows-only. |
| `capture_active_window(path)` | Whatever window is focused. Windows-only. |
| `get_open_editor_tabs()` / `get_active_window_title()` | Metadata |

Units everywhere: location **cm**, rotation/FOV **degrees**. Output resolution = current viewport size. PNG lands on disk (default `Saved/Revvy/Screenshots/`) — **Read the file; never pull image bytes into context as base64.**

## See → Act → Check loop (3-angle QA sweep)

After each meaningful build step, verify VISUALLY — a tool returning `success` is not proof:

1. **top-down** — camera above the build area, `rotation=(pitch=-90, yaw=0, roll=0)` → catches layout errors / overlaps
2. **eye-level** — `z≈170` (human eye height), level pitch → catches aesthetics
3. **player-eye** — street/gameplay level, slight up-tilt → catches gameplay feel/framing

For each angle: `describe_scene(pose)` → pick target → `capture_from_pose(same pose)` → Read the PNG → judge. Cross-check non-visually: `read_logs` for errors, `describe_scene` extents for interpenetration. **Fix the single weakest thing, re-capture, repeat.**

Rules:
- Deselect actors before capture (`show_ui=False` also strips gizmos/outlines automatically)
- Don't judge visuals while shaders compile (white/magenta wash) — verify via actors+logs instead
- Beauty shots: capture only; spatial reasoning: lean on `describe_scene` screen_px instead of burned-in labels

---

## Camera Best Practices for Screenshots

**Capture the current view by default — do NOT move the camera unless the user explicitly asks you to frame something a specific way.** Moving the camera is disruptive and requires the user to manually reset their view.

### When to Adjust Camera
- **Only** when the user explicitly says "show me X from above", "frame the landscape", or similar
- When taking verification screenshots of specific actors (e.g., water planes, splines)
- Never adjust the camera automatically as part of a general screenshot workflow
- The user's current viewport position is intentional — respect it

### Preferred: Use ActorService Camera View Methods

The `ActorService` provides methods to calculate and apply camera positions that frame actors:

```python
import unreal

actor_service = unreal.ActorService

# Move viewport camera to frame an actor from a specific direction
# Use unreal.ViewDirection (NOT unreal.EViewDirection — that prefix does not exist)
# Directions: TOP, BOTTOM, LEFT, RIGHT, FRONT, BACK
view = actor_service.get_actor_view_camera("MyLandscape", unreal.ViewDirection.TOP)
# Camera is now positioned — take screenshot immediately

# Calculate view without moving camera (for planning)
view = actor_service.calculate_actor_view("MyActor", unreal.ViewDirection.FRONT, 1.5)
# view.camera_location, view.camera_rotation available
# Apply when ready:
actor_service.set_viewport_camera(view.camera_location, view.camera_rotation)

# Directly set camera to any position/rotation (avoid this — easy to miss the subject)
actor_service.set_viewport_camera(
    unreal.Vector(1000, 2000, 500),
    unreal.Rotator(-45, 0, 0)
)
```

> ⚠️ **Critical**: Always use `unreal.ViewDirection.FRONT` — **never** `unreal.EViewDirection.FRONT`. The `E` prefix does **not** exist and will raise `AttributeError`.
>
> ⚠️ **Avoid guessing camera coordinates** with `set_viewport_camera`. Manual positions like `Vector(1200,-1200,600)` almost always capture only sky or empty space. Always prefer `get_actor_view_camera` which auto-calculates position from the actor's bounding box.
>
> For a diagonal/3-quarter view, use `calculate_actor_view` to get real bounds first, then offset from that position using trig — **never** guess coordinates. See the `level-actors` skill for the diagonal camera pattern.

**View directions:**
| Direction | Camera Position | Looking |
|-----------|----------------|---------|
| `TOP` | Above actor | Straight down (-Z) |
| `BOTTOM` | Below actor | Straight up (+Z) |
| `LEFT` | Left of actor (-Y) | Toward +Y |
| `RIGHT` | Right of actor (+Y) | Toward -Y |
| `FRONT` | Front of actor (+X) | Toward -X |
| `BACK` | Behind actor (-X) | Toward +X |

**Padding multiplier**: 1.0 = tight fit, 1.2 = 20% padding (default), 2.0 = double distance
Camera distance is automatically calculated from the actor's bounding box to fit it in view.

### FCameraViewInfo Result
- `success` (bool) - Whether calculation succeeded
- `camera_location` (Vector) - Calculated camera position
- `camera_rotation` (Rotator) - Calculated camera rotation
- `view_direction` (ViewDirection) - Direction used
- `actor_center` (Vector) - Actor bounds center
- `actor_extent` (Vector) - Actor bounds half-extent
- `view_distance` (float) - Distance from camera to actor center

### Legacy: Direct Camera Positioning (for custom angles)

Use `UnrealEditorSubsystem` to get/set the viewport camera:

```python
import unreal

editor_subsys = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

# Get current camera
camera_info = editor_subsys.get_level_viewport_camera_info()
if camera_info:
    cam_loc, cam_rot = camera_info

# Set camera to a new position/rotation
new_location = unreal.Vector(x, y, z)
new_rotation = unreal.Rotator(pitch, yaw, roll)
editor_subsys.set_level_viewport_camera_info(new_location, new_rotation)
```

### Focus on Actor Helper (only use when explicitly requested)

```python
import unreal

def frame_actor_for_screenshot(actor, distance=500.0, pitch=-25.0):
    """Position camera to look at an actor from a good angle."""
    editor_subsys = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    # Get actor bounds for center point
    origin, extent = actor.get_actor_bounds(False)

    # Position camera behind and above, looking at the actor
    yaw = 0.0  # Face default direction; adjust as needed
    rot = unreal.Rotator(pitch, yaw, 0.0)
    forward = rot.get_forward_vector()
    cam_loc = origin - (forward * distance)

    editor_subsys.set_level_viewport_camera_info(cam_loc, rot)
```

---

## Workflows

### Take Screenshot and Analyze (Recommended — use this every time)

```python
import unreal

# capture_editor_window is synchronous — file is ready immediately after the call
result = unreal.ScreenshotService.capture_editor_window("my_capture")
# Path is auto-normalized: saves to ProjectSaved/Revvy/Screenshots/my_capture.png

if result.success:
    print(f"SCREENSHOT_SAVED: {result.file_path}")
```

After executing, use `attach_image(file_path=result.file_path)` only when the
internal in-editor chat tool is present. External MCP clients must use their
native image attachment support or open the verified `result.file_path`.

> ⚠️ **NEVER use `capture_viewport` from Python.** It queues a write for the next engine render frame,
> which will not occur while Python is running on the game thread. The file will never appear on disk.
> `capture_viewport` now returns `success=False` with an explanation — if you see that, switch to
> `capture_editor_window` immediately. **Do NOT retry `capture_viewport`.**

### Check What User Is Viewing

```python
import unreal

tabs = unreal.ScreenshotService.get_open_editor_tabs()
for tab in tabs:
    print(f"{tab.tab_label} ({tab.tab_type})")

if unreal.ScreenshotService.is_editor_window_active():
    print("Editor is focused")
else:
    print(f"Focused: {unreal.ScreenshotService.get_active_window_title()}")
```

---

## Data Structures

### ScreenshotResult
- `success` (bool)
- `file_path` (str)
- `message` (str) - Error message if failed
- `width`, `height` (int)
- `captured_window_title` (str)

### EditorTabInfo
- `tab_label` (str) - Display name
- `tab_type` (str) - Asset class type
- `asset_path` (str)
- `is_foreground` (bool)

---

## Attaching Images for Vision Analysis

After capturing in Revvy's in-editor chat, use `attach_image` only when that
internal tool is present. It is not a Python function:

```
attach_image(file_path="E:/Screenshots/Capture.png")
```

- **This is an internal in-editor tool call**, not Python code and not part of the external MCP manifest. Do NOT put it inside `execute_python_code`.
- External MCP clients must use their native image attachment support or open the verified screenshot path.
- Supported formats: PNG, JPG, BMP, GIF, WEBP
- File must exist on disk before attaching
- Image will be included in your **next** LLM request for vision analysis
- Use this after screenshots, satellite images, or any image you need to visually analyze

### Common Uses
- **Screenshots**: Capture viewport → attach → analyze what you see
- **Satellite images**: `terrain_data get_map_image` → attach → identify terrain features for material/painting
- **Blueprint graphs**: Capture editor window → attach → review node layout
- **Verification**: Capture result → attach → confirm changes look correct
