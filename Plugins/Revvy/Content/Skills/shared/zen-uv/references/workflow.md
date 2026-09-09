# Zen UV — module reference

Detail for each Zen UV module. Trim sheets live in `trimsheet.md`; failures live in
`troubleshooting.md`.

The Combo Panel is the hub: a column of section icons in the N-panel, the Properties
editor (when the Transform Tool is active), or under the cursor with `Shift + U`.
Subpanels can be pinned open — red icon means pinned, cyan means open, grey means closed.
**The panel adapts to context, so operators that need 3D information simply do not appear
in the UV Editor.**

---

## Unwrap and the Mark System

### Zen Unwrap

The main button. What it does depends entirely on what is selected when it runs.

| Selection | Result |
|---|---|
| Nothing | Unwraps the whole mesh from existing seams. With no seams it offers to create them. |
| Faces | Creates seams around the selected polygons, making them a new island, then unwraps. |
| Edges | Marks the selected edges as seams, then unwraps. |
| Vertices | Re-unwraps the touched islands **without** adding seams or splitting anything. |

`Processing Mode` decides the blast radius: **Whole Mesh** re-unwraps everything including
already-unwrapped islands; **Selected Only** touches just the selection. Starting in
Selected Only with nothing selected raises a confirmation before it processes everything.

Unwrap methods: **Conformal** (fast, good) and **Angle-Based** (slower, more accurate).

Edge case: on a closed mesh such as a sphere, selecting *all* faces makes the operator
remove every seam and then fail to create new ones, because there are no borders left. It
warns instead of unwrapping.

### Mark System

- **Mark by Angle** — seams and/or sharps from a dihedral angle threshold. 3D Viewport only.
- **Mark** / **Unmark** — selected edges or face borders.
- **Unmark All** — strip every seam and sharp, optionally clearing pins too.
- **Global Mark Settings** — one switch pair (Seam / Sharp) that every Mark operator
  inherits, instead of setting it per operator.

### Conversion System

Converts between the three edge representations in any direction: Seams by UV Borders,
Sharp by UV Borders, Seams by Sharp Edges, and the inverses. This is how you recover seams
from a mesh that arrived already unwrapped.

### Mirror Seams

Mirrors seams along `X`/`Y`/`Z` in a `+` or `-` direction, either replacing or adding to
what is there. If the source side has no seams it aborts rather than wiping the target
side. Hold `Shift` to flip both directions at once.

### Auto Seams & Unwrap

Segments the mesh into planar charts, places seams on the chart borders, and optionally
unwraps — one pass, no manual seam work. Works on both organic and hard-surface meshes
and is tuned to avoid micro-island fragmentation.

### Auto Unwrap (Ministry of Flat)

A bridge to Eskil Steenberg's Ministry of Flat. **Windows only** — the console binary has
no Linux or macOS build, so the operator is absent on those platforms.

Grouped settings: Preprocess (force hard surface, separate by material or sharp edges,
triangulate), Postprocess (mark seams, fix self-intersecting UV faces), Texel Density
(texture size, TD target), MOF core (auto-detect hard edges, use normals), and Advanced
(stretch, packing, cut, squares). The manual reproduces the MOF command-line reference
including `-RESOLUTION` (default 1024), `-SEPARATE`, `-ASPECT`, `-NORMALS`, and UDIM flags.

### Prepare Unwrap

Resets the object: removes UV maps, seams, sharps, and Finished marks. Apply geometry
modifiers *before* running it.

### Smooth by Sharp

Blender ≥ 4.1 sets Shade Smooth on all faces. Older Blender toggles Auto Smooth 180° with
sharp edges.

### Finishing System

Tags islands you are done with.

- **Sort Islands by Tags** — finished islands move right of the main UV tile, unfinished
  move left, so progress is visible at a glance.
- Finished islands are protected from accidental re-unwrapping.
- `Ctrl` on the Checker pie sector toggles the Finished display.

---

## Seam Groups

Stores several named seam sets on one mesh and switches between them — useful when the
same asset needs different UV cuts for, say, baking versus lightmapping.

`Assign` (seams → group), `Activate` (group → mesh), `Assign Selection`, `Remove
Selection`, plus add/delete group. **Selecting a group in the list does not apply it** —
you must run `Activate`.

---

## Select

Filters that find problem islands instead of hunting for them by eye.

### Island filters

| Operator | Finds |
|---|---|
| Select Islands | Whole islands from any selected element |
| Select Overlapped | Islands sitting on top of each other |
| Select Flipped | Mirrored / negative-area islands |
| Select Quaded Islands | Islands made only of quads |
| Select Holed Islands | Islands with interior holes |
| Select Similar | Islands matching the selected one (Area Matching can be relaxed) |
| Select in Tile | Islands inside the active UDIM tile or UV area |
| Select Islands By Direction | Islands facing a given direction |
| Filter Islands by Geometry | By face count / polygon vertex counts — isolates stray triangles, n-gons, single-face islands |
| Filter Islands by Property | Grouped by an arbitrary property |

### Face filters

| Operator | Finds |
|---|---|
| Select Stretched Faces | Distortion above a threshold, by area or by interior angle. Pairs with Blender's *Overlay → UV Stretch → Area/Angle*. |
| Select Self-Intersecting Faces | UV polygons that overlap themselves — common with n-gons or third-party unwrappers |
| Select by UV Value | Faces matching a UV-space condition; `Get Selected Area` feeds the measured area back into it |
| Select Zero Area Faces | Preset of the above with condition Zero Area, threshold 0 |
| Select Faces Less than Pixel | Faces smaller than one texel |

Most of these take a `Clear Selection` option that resets the selection first.

---

## Transform System

Three layers, all doing related work:

1. **Unified Transform System** — the Universal Control Panel, a 3×3 grid whose buttons
   change meaning with the active transform type: direction for Move, scale origin for
   Scale, rotation direction for Rotate, flip axis for Flip, fit origin for Fit, alignment
   edge for Align.
2. **Advanced Transforms** — standalone operator equivalents that do not need the grid,
   plus targeted relocation (Move Island, Move to UV Area, Move 2D Cursor To) and UDIM
   routing.
3. **Independent Transform Operators** — geometry reconstruction that does not fit the
   unified model.

Shared modifiers: **Transform Space** (Island vs Texture — Texture only applies to Move
and Rotate), **Mode** (Islands vs Selection), and **Order** (One by one / Overall /
System Pivot).

### Independent operators worth knowing

- **Relax** — three algorithms; the organic-model workhorse and the answer to "Zen UV
  can't unwrap organic shapes".
- **Relax Along Axis** — axis-constrained relax. UV Editor only, because an axis cannot be
  defined in the 3D view. Works best relaxing selected vertices against unselected ones.
- **World Orient** — rotates islands to match their orientation on the model, with
  Organic and Hard Surface heuristics plus an optional extra axis-alignment pass.
- **Quadrify Islands** — straightens rectangular quad strips into a clean grid.
  **It marks seams across the whole mesh according to island distribution**; turn off
  *Mark Borders* if that is not wanted. This surprises people regularly.
- **Reshape Island** — presets: `Selected` (straighten selected loops, relax the rest),
  `U/V Direction` (align edges to an axis), `Borders` (rebuild from corner or pinned
  points). Corner detection is by vertices with exactly two connected edges, or by
  Blender's native pins.
- **Match and Stitch** — matches position, rotation, and scale of islands, then optionally
  stitches vertices. **Stitching is a separate option** — if it only matched, that option
  was off.
- **Randomize** — jitters position/rotation/scale of islands or elements; simple and
  advanced modes.
- **Distribute** — Distribute & Sort along an axis for islands, or distribute vertices
  along a line.

### Fit and Align

`Fit` scales islands into the UV square; `Face by Face` treats each face separately.
`Fit into Region` fits into an arbitrary rectangle you can grab from a selection or island
and preview with annotations. `Align` has a `Vertex by Vertex` mode for exact vertex
alignment in Selection mode.

---

## Stack

Makes topologically identical islands share one UV footprint — the standard preparation
for baking duplicated geometry.

Islands are classified and colour-coded in the viewport:

| Type | Colour | Meaning |
|---|---|---|
| Primary | orange-red | The reference island; its layout is copied onto the replicas |
| Replica | muted blue | Matches a primary; its UVs get overwritten |
| Single | emerald green | No topological match found |

The primary is chosen automatically as the island nearest the UV origin, unless
`Primary Source` is set to Pinned or another explicit method.

### Matching modes — pick deliberately

| Mode | Matches on | Risk |
|---|---|---|
| **Exact Match** | Topology **and** 3D size | None. Start here. |
| **Topology & Scale** | Shape, ignores 3D size | Stacking parts of different real-world size gives **uneven texel density** |
| **Topology Only** | Vertex connectivity only, ignores edge lengths | Loosest; intended for manual stacking |

Recommended flow: `Stack All (Exact)` → inspect the colour overlay → only then loosen the
mode for the leftovers. Settings live in a global popup on the panel header and are
inherited by every stack operator, with per-operator overrides available.

---

## Texel Density

TD is texels per unit of surface — normally quoted as px/cm (e.g. 2.56 px/cm) or px/m
(256 px/m). Consistent TD is what stops one asset looking sharper than its neighbour.

- **Get TD** / **Set TD** — sample from a reference island, apply to the rest.
  Set TD has **Overall** mode (all selected islands together, preserving their relative
  sizes) and **Island** mode (each island individually).
- **Use Pivot** — scale from centre or any of eight directions when setting TD.
- **TD Calc Precision** — what percentage of an island's polygons get sampled.
- **Get TD Range** — current min / average / max across selected objects or the whole
  scene; clicking a number selects the islands at that value.
- **Calculate TD** — solves for the optimal texture size from a target TD, or vice versa.
- **Display TD** — viewport overlay per island or per face. Colour schemes: Three Color
  (less / equal / greater than target), Full Spectrum, Linear, Presets.
- **Presets** — save, load, and share TD values so a team stays consistent. Each preset
  gets its own one-click Set button.
- **Select by TD** — select islands at a given density.

---

## Pack

- **Pack Islands** runs the selected engine. Supported engines: **Blender Pack**,
  **UV Packmaster**, **UV Packer**. Only the main preferences of each are surfaced in the
  Zen UV panel; the rest stay in that engine's own UI.
- **Margin** is the gap between islands. Too small and texture filtering bleeds between
  islands in-engine. **Calculate Recommended Margin** derives it from the real image size.
- **UV Coverage** reports used versus empty UV space. Higher coverage means higher
  achievable texel density.
- **Pack To** routes islands somewhere other than the default `0–1` square — inside or
  outside trim frames, or a specific UDIM tile. Blender and UVPackmaster engines only.
- **Excluded System** tags islands to keep out of packing and can offset them out of the
  way. Blender and UVPackmaster engines only.
- Engine-specific options: `Fixed Scale` and `Lock Overlapping` modes and `Packing Mode`
  are UVPackmaster features; `Margin Method` is Blender-only.
- `Average Islands Scale`, `Rotate Islands`, and `Pack Selected Islands` are common.

---

## UV Checker and the Draw System

- **Checker Texture** (`Alt + T`) inserts a Zen UV checker node on top of existing
  materials and removes it again — original materials are not modified. `Remove Checker
  Nodes` cleans every material in the scene.
  This is the one documented scripting entry point:
  `bpy.ops.view3d.zenuv_checker_toggle(action='ON')` (also `'OFF'`, `'TOGGLE'`).
- Every bundled checker texture has a **bright green pixel at UV (0, 0)**. Green faces in
  the viewport therefore mean *those faces have no UV coordinates*.
- The checker library is a folder of `.jpg`/`.png`; point it anywhere and refresh.
  Textures can be filtered by resolution, with a lock for square-only.
- **Blender Draw System** overlays crease, sharp, and bevel weights.
- **Zen UV Draw System** overlays Seams, Sharp, Finished, Flipped, Overlapped, and
  Stretched. Each overlay has a companion select operator. Enable **Stretched Dynamic**
  for a live stretch map.
- **Darken Image** dims the background image in the UV Editor so the overlays read.

---

## Advanced UV Maps

Quick access to Object Data → UV Maps, but across **multiple selected objects at once**.

- Add / remove / rename / set active, in single-object or multi-object mode.
- **Status detection** flags UV maps that are out of sync between objects (name, index,
  active, render flags), and **Sync UV Maps** repairs them. Do this before binding a
  hotkey to "set active UV map by index", or the index will mean different things on
  different objects.
- **Sync Seams by UV Islands** rewrites seams to match the islands of the newly active map.
- **Copy UV / Paste UV** transfers coordinates between maps — Edit Mode, and only when the
  selected objects share geometry.
- **Transfer UV** moves UVs between objects.
- **Clean UV Maps** removes all inactive maps.
- Warning: deleting **all** UV maps also drops every Finished and Excluded tag.

---

## Interface extras

- **Sticky UV Editor** — opens a UV Editor to the left, right, or as a separate window via
  a viewport widget or shortcut. `Ctrl + Shift + LMB` on the widget opens its settings.
  Optionally remembers UV Editor overlay settings between openings. (Originally a separate
  add-on by Oleg Stepanov / DotBow.)
- **Zen Touch Tool** — a UV Editor gizmo with a Pivot handle, an Angle handle, and a Line
  handle. **Setup Mode** positions the gizmo without touching UVs; **Transform Mode**
  drives islands or selections. Double-click shortcuts: P-handle aligns the gizmo along
  (then across) the island in Setup Mode, and applies World Align in Transform Mode;
  A-handle double-click applies Set TD, `Ctrl` + double-click samples Get TD.
- **Favourites** — collect frequently used operators into one panel, displayed at the top
  of the N-panel, as its own tab, or as a floating panel.
- **Save as Default Operator Properties** — right-click an operator property to store your
  preferred default, so it survives Blender restarts. Supported by Zen Unwrap, Mark by
  Angle, Quadrify, and others.
- **User Script** — a Python file with `register()` / `unregister()` that Zen UV executes
  after it loads, for changing operator defaults or UI layout without patching the add-on.
  Keep it outside the add-on folder so upgrades do not overwrite it.
