# Zen UV — troubleshooting

Distilled from the official FAQ and the public support forum
(<https://support.zenmasters.team/c/zenuv/5>). Where an answer came from the vendor it is
their position, not a guess.

## First questions to ask

Almost every vendor reply opens with the same three:

1. Which Blender version?
2. Which Zen UV version? (A large share of reports are fixed by updating.)
3. Is **UV Sync Selection** on or off?

Add a fourth for anything geometry-related: are there unapplied modifiers?

## "I can't find the button"

The Combo Panel adapts to context. Operators needing 3D information do not exist in the
UV Editor.

| Missing | Why |
|---|---|
| Mark by Angle | 3D Viewport only — only the 3D mesh has dihedral angles |
| Quick Hotspot | 3D Viewport only — the UV Editor has no sharp-edge data |
| Relax Along Axis | UV Editor only — the axis is undefined in 3D |
| Reshape Island | UV Editor panel |
| Auto Unwrap | Windows only; absent on macOS and Linux |
| Pixel size fields for a trim | Only when the trim belongs to an **Image**, not a Scene |

"Quadrify selected faces on UV" is `Quadrify Islands` in the Transform panel.

## Zen Unwrap does nothing / behaves oddly

- It does **not** run with **UV Sync Selection off** in the UV Editor.
- Use a **single** select mode. Vert+Edge or Edge+Face combinations are ignored.
- Selecting *every* face of a closed mesh (a sphere) makes it clear all seams and then
  fail to create new ones — there are no borders left. It warns instead.
- `Unfold Vertices` was removed from Zen Unwrap in recent versions; it is no longer needed.
- "Zen Unwrap doesn't combine islands" — combining is not what it does. To merge islands
  that already share seams, remove the seam and re-unwrap, or use Stack for duplicates.

## Quadrify puts seams where I didn't select

Expected behaviour. **Quadrify marks seams across the entire mesh** according to how
islands are distributed in the UV Editor, not according to your selection. Disable
**Mark Borders** in the operator properties if you do not want that.

## Stack problems

- "Stacking selected stacks everything" — check the operator's scope and the global Stack
  settings in the panel-header popup; every stack operator inherits them.
- Uneven texel density after stacking means **Topology & Scale** or **Topology Only**
  matching was used on parts with different real-world sizes. Use **Exact Match** unless
  you specifically want scale-independent stacking.
- Unstack issues have historically needed a repro file — the vendor asks for the object,
  not a video.

## Match and Stitch only matches, never stitches

**Stitch is a separate option and it is off by default.** Enable it in the operator
properties.

## Texel density looks inconsistent

Use `Get TD Range` to see min / average / max across the selection or scene, then click a
value to select the islands sitting at it. `Display TD` with the Three Color scheme makes
the outliers obvious. Remember `Set TD` has **Overall** (keeps relative island sizes) and
**Island** (normalises every island) modes — they give very different results.

## Pack engine not detected

"No UVPackmaster properties found" means Zen UV cannot see the packer. Verify the
UVPackmaster version is one Zen UV supports — support for new major versions (e.g.
UVPackmaster 4) lands in a Zen UV update, not automatically.

## Checker shows green faces

Every bundled checker texture has a bright green pixel at UV (0, 0). Green faces mean
**those faces have no UV coordinates**, not that the texture is broken.

Checker nodes are added on top of existing materials and can be stripped with
**Remove Checker Nodes**. Original materials are never modified.

## Icons disappeared from the panel

All Blender add-on icons share one data-block. If any add-on corrupts it, every add-on
loses its icons. Check the system console for the add-on that threw, and disable it.

## Install and update

- Update via the add-on's own **Update** button has historically installed into the
  **default** add-on folder even when Zen UV lives in a custom scripts folder. If you use
  a custom folder, install manually.
- Upgrading with Blender's *Copy New Settings* option has produced broken installs.
  The workaround is to uninstall and reinstall cleanly.
- macOS: the Zen UV core library can be quarantined by Gatekeeper. Follow the vendor
  installation page rather than dragging files by hand.
- Linux distributions are effectively untested; report with the distribution name.
- Extension-platform installs (Superhive Extensions, ZenBundle) have had teething issues
  separate from the classic add-on zip.

## "Add-on may not work correctly in current Blender version"

Shown when Zen UV runs on a Blender newer than its supported range — Alpha, Beta, or
Daily builds. Those builds change the Python API frequently. Compatibility updates ship
after the stable Blender release, not before.

Supported range: Blender 4.2 LTS and newer official releases, following Blender's own
two-year LTS support window.

## Reset an operator property to its default

Right-click the property. To make your own value the default instead, use
**Save as Default Operator Properties** (also right-click), which persists across restarts.

## Modifiers

Working on a mesh with unapplied modifiers — mirror in particular — puts you in
"procedural" territory where UV operators act on the base mesh, not the modified result.
Apply modifiers before the UV pass, or expect the generated geometry to be untouched.

## Organic models

Yes, Zen UV handles them: **Relax** (three algorithms) is the intended tool. The
"Minimum Stretch" algorithm people request has been part of Relax since 2022.

## Reporting a bug upstream

The vendor consistently asks for a **minimal reproducible .blend**, plus Blender and Zen
UV versions. A video alone is usually not enough to act on.
