---
name: zen-uv
description: Plan and explain UV work with the Zen UV add-on for Blender — Zen Unwrap and the Mark system, seam groups, island selection filters, transform/quadrify/relax, stacking, texel density, packing engines, UV checker, and trim sheets with Hotspot Mapping. Use for Zen UV questions, trim sheet or hotspot workflows, texel density targets, UV stacking for baking, pack margin choices, 젠유브이, UV 언랩, 트림시트, 핫스팟, 텍셀 밀도, UV 패킹. Zen UV is a paid add-on whose Python API is undocumented; use the verified operator list in references/operators.md and never invent operator IDs.
---

# Zen UV

Zen UV is a commercial UV toolkit for Blender from Zen Masters. This skill carries the
product knowledge: what each module does, which operator solves a given problem, and the
order the tools are meant to be used in.

## Scope

**Product knowledge** — recommend the right tool and settings, explain trim sheet and
hotspot workflows, diagnose failures, describe how a feature behaves.

**Scripting** — possible, but only against verified identifiers. The published manual is
a user guide: across all 124 pages it names exactly **one** `bpy.ops` identifier
(`bpy.ops.view3d.zenuv_checker_toggle`). Everything else in `references/operators.md` was
enumerated from Blender's RNA on a licensed install of **Zen UV 5.3.3** — 252 operators,
33 keymap entries, every property readable.

**Never invent a `bpy.ops.*zenuv*` identifier.** If it is not in `references/operators.md`,
re-enumerate against the installed version rather than guessing:

```python
# Blender console or --background --python
for mod in dir(bpy.ops):
    for op in dir(getattr(bpy.ops, mod)):
        if "zenuv" in ("%s.%s" % (mod, op)).lower():
            print("%s.%s" % (mod, op))
```

Then read each operator's properties through `get_rna_type().properties`. The add-on ships
as readable Python (~250 modules), so behaviour can be confirmed in source when the RNA
description is thin.

## Hard constraints

| Constraint | Detail |
|---|---|
| Licence | Paid add-on (Superhive/Blender Market, Gumroad). Not redistributable. Current line is 5.x. |
| Blender | 4.2 LTS and newer official releases. Alpha/Beta/Daily builds raise a compatibility banner and may misbehave. |
| Auto Unwrap | **Windows only.** It is a bridge to Ministry of Flat (`UnWrapConsole3.exe`), which has no Linux or macOS build. |
| Core Library | `zen_uv_lib_<platform>_v2_0_0.<dll\|so\|dylib>` is a **separate download** installed from the add-on preferences or the side panel. It wraps the heavy UV maths. Without it the preferences show *not installed* and the dependent tools are unavailable. Zen Relax ships with the add-on and is not affected. |
| UV Sync | Zen Unwrap does **not** run with UV Sync Selection off in the UV Editor. |
| Selection mode | Zen Unwrap needs a single mesh select mode (Vert *or* Edge *or* Face). Mixed modes do nothing. |
| Context | Mark by Angle and Quick Hotspot exist only in the 3D Viewport — the UV Editor has no angle or sharp-edge information. |

## Default shortcuts

| Keys | Action |
|---|---|
| `Alt + U` | Zen UV pie menu |
| `Shift + U` | Zen UV pop-up combo panel under the cursor |
| `Alt + T` | Checker texture toggle |

Pie sectors are modifier-sensitive: Quadrify (`Ctrl` Relax, `Shift` Hotspot Mapping),
Zen Unwrap (`Alt` Pack, `Shift` Transform Tool), Checker (`Ctrl` Display Finished,
`Alt` Display Stretch Map). Tap the hotkey — holding it dismisses the alternates.
Rebind under *Edit → Preferences → Add-ons → Zen UV → Keymap*.

## The canonical workflow

Follow this order. Most Zen UV problems come from doing these out of sequence.

1. **Prepare** — apply geometry modifiers first, then `Prepare Unwrap` to strip UV maps,
   seams, sharps, and Finished tags for a clean start.
2. **Mark seams** — `Mark by Angle` for a first pass, then `Mark`/`Unmark` by hand. The
   Conversion System converts between Seams, Sharp edges, and UV borders in any
   direction. `Mirror Seams` copies seams across an axis on symmetric meshes.
3. **Unwrap** — `Zen Unwrap` is the context-sensitive main button. On a hard-surface
   model use Face or Edge select mode; use Vertex mode to re-unwrap an island *without*
   adding seams. `Relax` handles organic shapes; `Quadrify Islands` straightens
   rectangular strips.
4. **Orient** — `World Orient` rotates islands to match how they sit on the model.
5. **Stack** — `Stack All (Exact)` first; it only groups islands with identical topology
   *and* identical 3D size, so it cannot distort anything. Loosen the matching mode only
   when you know the duplicates differ in scale.
6. **Texel density** — `Get TD` from a reference island, `Set TD` to the rest, then
   `Display TD` to verify. Presets keep a project consistent across a team.
7. **Pack** — set `Margin` (use `Calculate Recommended Margin` against the real texture
   size), pick a pack engine, then `Pack Islands`. Check `UV Coverage` afterwards.
8. **Verify** — `Alt + T` checker, plus the Display overlays for Flipped, Overlapped,
   Stretched, and Finished islands.

Mark islands **Finished** as you go. The Finishing System sorts finished islands to one
side of the UV tile and protects them from being re-unwrapped.

## Module map

| Need | Module | Reference |
|---|---|---|
| Seams, unwrapping, finishing | Unwrap / Mark System | `references/workflow.md` |
| Several seam sets on one mesh | Seam Groups | `references/workflow.md` |
| Find bad islands (flipped, overlapped, stretched, zero-area) | Select | `references/workflow.md` |
| Move / rotate / scale / fit / align / distribute | Transform System | `references/workflow.md` |
| Share UVs between duplicate parts | Stack | `references/workflow.md` |
| Consistent texel density | Texel Density | `references/workflow.md` |
| Pack into 0–1 or UDIM | Pack | `references/workflow.md` |
| Visual QA | UV Checker / Draw System | `references/workflow.md` |
| Multiple UV maps, transfer UVs | Advanced UV Maps | `references/workflow.md` |
| Reuse one texture across many assets | Trim Sheets, Hotspot Mapping | `references/trimsheet.md` |
| Something is broken or missing | FAQ and known issues | `references/troubleshooting.md` |
| An exact `bpy.ops` identifier or property | Verified operator list | `references/operators.md` |

## Answering style

State which panel the operator lives in and which editor it works in — a large share of
support questions are "I can't find the button", and the answer is usually that the
operator is 3D-Viewport-only. When a setting has a safe default and a risky one, name
both and say which one distorts geometry.

## Sources

Public documentation, GPL-3.0: <https://github.com/ZenMastersTeam/Zen-UV> ·
<https://zenmastersteam.github.io/Zen-UV/latest/>. Support forum:
<https://support.zenmasters.team/c/zenuv/5>. This skill is a summary — quote the manual
rather than copying it wholesale.
