# Zen UV — trim sheets and Hotspot Mapping

## What a trim sheet buys you

One texture holds a strip of reusable detail — panel edges, pipes, trims, grates. Every
asset in a set maps onto that one sheet, so a whole environment ships with a single
material instead of dozens of unique bakes. Reuse is the entire point: a scene with many
recurring elements is the ideal candidate. This is standard practice for AAA environment
art.

## Where a trim sheet lives

A trim sheet is stored either in a **Scene** data-block or in an **Image** data-block.

| Storage | Active sheet behaviour |
|---|---|
| **Scene** | One sheet per scene; never changes |
| **Image** | Depends on the current editor — the UV Editor's active image selects the sheet |

This distinction has a practical consequence: **trim sizes can only be entered in pixels
when the trim belongs to an Image.** A Scene-owned trim is expressed in UV units. If
someone cannot find a pixel input, that is why.

## Creating trims

Activate the ZenUV tool in the UV Editor, choose category **Trims**, mode **Create**, and
drag out rectangles.

Snapping is toggled with the magnet in the ZenUV tool header:

| Snap mode | Snaps to |
|---|---|
| Pixels | Image pixels (needs an active image) |
| Grid | The UV grid |

The **Trims List** manages the set — select, rename, reorder, delete. It sorts by the
standard Blender methods plus by width and height. Zen UV also harvests naming prefixes
automatically (`TopPart_01`, `Radial_01`, `Trim_19` → `TopPart`, `Radial`, `Trim`) and
offers them as filters, so a consistent naming convention pays off immediately.

Exactly one trim is active at a time; it is highlighted in the list and shown with
handles in the editor. The **Trim Preview** subpanel lets you pick the active trim from a
thumbnail, with optional **Auto Fit** to fit immediately on selection.

## Interchange

- **SVG import/export** — trim layouts round-trip through SVG, so they can be authored in
  Inkscape or any vector editor and shared without matching Blender versions.
- **Substance 3D Designer** — export trim masks to build the trim texture procedurally.
- **DECAL Machine** — import that add-on's textures and trim layout into Zen UV.

## Trim operators

All of them act on the **active** trim (except `Fit to Selected Trims`) and respect
`Mesh Selection Mode` (Islands vs Selection) and `Processing Order` (One By One =
separate pivots, Overall = one shared pivot).

| Operator | Effect |
|---|---|
| Move In Trim | Move islands inside the active trim |
| Rotate In Trim | Rotate inside the active trim |
| Scale In Trim | Scale inside the active trim |
| Flip In Trim | Flip about the trim centre |
| Fit To Trim | Fit islands into the trim; `Transform Faces` treats each face as its own island |
| Align To Trim | Align to the trim; `Transform Vertices` aligns each vertex separately |
| Fit to Selected Trims | Randomly distributes selected islands across **all selected** trims |

`Lock in Trim` makes Move and Scale ignore islands that are outside the active trim.

## Hotspot Mapping

Automatic mapping that matches island parameters against trim parameters and places each
island in the best-fitting trim. The original technique is Valve's.

Reach it from the Trim Operators panel, or the pie menu (`Shift + U`, sector 1, holding
`Shift`).

### Settings that matter

**Preprocess**
- `Mode` — map into *All Trims* or only *Selected Trims* (shown as blue frames).
- `Orient` — pre-rotate islands before matching.

**Matching**
- `Allow Rotation` — permit 90° rotation for a better fit.
- `Keep Proportions` — *Yes* preserves the island's aspect ratio when it lands in a trim;
  *No* lets it stretch. Say which one you mean; it changes the result completely.
- `Area Matching` — *As is* picks the trim with the closest area, *Max* picks the largest
  (highest texel density), *Min* picks the smallest (lowest texel density).

**Variability** — stops a tiled wall from looking obviously repeated.
- `Allow Rotation Variation` — random 180° flips.
- `Allow Location Variation` — lets an island land in a *different* trim with similar
  parameters. **Requires two or more near-identical trims**, otherwise it does nothing.
- `Allow Offset Variation` — shifts the island within its trim. **Only works when the
  operator's `Fit Axis` is set to *Trim Settings* and the trim's own advanced `Fit Axis`
  permits it.** This is the usual reason "offset variation does nothing".

Note that `Allow Location Variation` and offset variation are unrelated settings that get
confused for each other in support threads.

### Quick Hotspot

The fast path: unwrap plus several preprocessing steps in one operator. It works with
existing islands or creates them from selected polygons.

**3D Viewport only** — the UV Editor has no sharp-edge information, so predictable islands
cannot be generated there.

## Known limitation

Hotspot Mapping keeps islands inside the trim rectangle. Making islands extend past the
`0–1` trim space (for tiling strips that should repeat beyond the sheet) is not what the
operator is designed to do; that has been raised on the support forum.
