---
name: tripo-blender-character-pipeline
description: Step-by-step, semi-automated character creation pipeline — AI multi-view reference sheets, Tripo Studio image-to-3D per part, Blender retopo/UV/texture/rig/scene — with Reddy driving every automatable step through the Blender MCP and image-generation skills. Use for Tripo, 트리포, 이미지로 3D 모델, AI 캐릭터 모델링, 멀티뷰 레퍼런스, 캐릭터 파이프라인, image-to-3D character workflow, or when the user wants to follow the Blender+Tripo modeling course flow step by step.
compatibility: Blender MCP required for the automated steps; Tripo Studio (studio.tripo3d.ai) account required for stage 2; RetopoFlow / Quad Remesher / Auto-Rig Pro are optional paid add-ons with free fallbacks noted per stage.
---

# Tripo + Blender Character Pipeline (guided, semi-automated)

You are the pipeline conductor. The user follows a 6-stage character workflow
(the same arc as the Blender+Tripo modeling course: reference sheets → Tripo
per-part generation → Blender cleanup/retopo → UV+texture → rig → scene).
Your job on every stage:

1. Say which stage the user is on and what DONE looks like for it.
2. Run everything automatable yourself (image generation, Blender MCP calls).
3. For manual steps (Tripo web UI, sculpt-level cleanup, paint), give exact
   click-level instructions, then WAIT and verify the result before advancing.
4. Verify completion with real checks (file exists, Blender scene state via
   MCP) — never assume a manual step happened.

Keep a visible checklist in every reply while this pipeline is active:

```
[✓] 1. 레퍼런스 시트   [→] 2. Tripo 생성   [ ] 3. 리토포   [ ] 4. 텍스처   [ ] 5. 리깅   [ ] 6. 씬/렌더
```

Ask which stage to start from when the user arrives mid-pipeline (e.g. they
already have Tripo exports).

---

## Add-on setup — check first, then install what's installable

Before stage 3 (and again before stage 5 if skipped), audit the add-ons this
pipeline uses and set up everything that can be set up automatically. Always
CHECK and REPORT first; download/install only after the user agrees.

Audit via Blender MCP:

```python
import addon_utils
wanted = {"rigify", "retopoflow", "quad_remesher", "auto_rig_pro"}
found = {m.__name__: addon_utils.check(m.__name__) for m in addon_utils.modules()
         if any(w in m.__name__.lower() for w in wanted)}
# addon_utils.check -> (default_enabled, currently_loaded)
```

Then, per add-on:

- **Rigify** — ships with Blender. If not enabled:
  `addon_utils.enable("rigify", default_set=True)` then
  `bpy.ops.wm.save_userpref()`. No download, safe to do on a yes.
- **RetopoFlow** — free/GPL; releases are public on GitHub
  (`CGCookie/retopoflow`). With the user's OK: query the GitHub API for the
  latest release, pick the `.zip` asset, download it to a temp path, then
  `bpy.ops.preferences.addon_install(filepath=...)` +
  `addon_utils.enable(...)` + save userpref. Match the Blender major version
  (RetopoFlow 3 targets Blender 3.x; check the release notes for 4.x) and say
  which version you installed.
- **Quad Remesher / Auto-Rig Pro** — commercial. NEVER auto-install; report
  installed-or-not and link the official pages (exoside.com,
  blendermarket.com/products/auto-rig-pro) if missing. The per-stage free
  fallbacks below keep the pipeline working without them.

Report the audit as a table (add-on / status / action taken) and only then
enter the stage.

---

## Stage 1 — AI 멀티뷰 레퍼런스 시트 (Reddy가 자동 생성)

Goal: a reference set Tripo can consume per part, named exactly:

```
head-front.jpg  head-left-profile.jpg  head-right-profile.jpg  head-back.jpg
Body_front.jpg  Body_left.jpg  Body_right.jpg  Body_back.jpg
eye-front.png  iris.png  nose-front-closeup / nose-side-view.png
eyebrow-front / eyebrow-side.png  eyelash-front / eyelash-side-closeup.png
```

Do it: use the `gpt-sol-image-generation` skill (or the user's preferred image
model). Generate in this order, one consistent character:

1. Character concept sheet (full body, T-pose or A-pose, neutral expression).
2. Head turnaround: front / left profile / right profile / back — same scale,
   same lighting, plain light-gray background, orthographic look, NO shadow.
3. Body turnaround: same four views, arms in A-pose, no overlapping limbs.
4. Part close-ups on plain background: eye (front), iris alone, nose (front +
   side), eyebrow (front + side), eyelash (front + side).

Prompt skeleton (fill the character description once, reuse verbatim for
consistency): "character turnaround sheet, {front|left profile|right profile|
back} view, full {head|body}, orthographic, flat neutral studio lighting,
plain #DDDDDD background, no shadow, consistent proportions, 3D modeling
reference". Save into `<project>/ref/` with the names above.

Verify: all files exist; views match (open 2-3 and compare silhouettes).
Common failure: the model drifts between views — regenerate the odd view out
with the SAME prompt plus "same character as attached" + the front view
attached.

## Stage 2 — Tripo Studio 이미지→3D, 파츠별 (수동, 클릭 가이드)

Goal: one generated mesh per part — head, body, and thin parts (eyebrow,
eyelash, nose if separate) — quad topology, exported for Blender.

Guide the user through studio.tripo3d.ai per part:

1. 3D 워크스페이스 → 모델 생성 → **HD 모델** + **스마트 메시** on.
2. Upload the part's multi-view set (front + left + right + back where it
   exists; close-ups for thin parts).
3. 토폴로지: **Quad**. Target polygons: head/body ≈ default; thin parts low.
4. Generate (uses credits — say so BEFORE they click), inspect in viewport,
   regenerate with a cleaner reference if the silhouette is wrong.
5. Export **FBX or GLB** per part into `<project>/tripo/` — or use Tripo's
   **DCC Bridge** add-on to push straight into Blender if installed.

Verify: exported files exist in `<project>/tripo/` (list them). Record which
part each file is; you need that mapping in stage 3.

## Stage 3 — Blender 임포트 + 리토포 (반자동)

Goal: clean editable meshes — REF originals preserved, LOW versions built.

Run the add-on audit above first (RetopoFlow can be auto-installed here with
the user's OK). Then automate via Blender MCP (`blender-3d-modeling` skill):

1. Import every export from `<project>/tripo/`; normalize scale/rotation.
2. Collections: `REF` (Tripo originals, hidden+non-selectable), working copies
   named `Head`, `Body`, `Eyebrow`, `Eyelash`, …
3. Mirror setup on symmetric parts: delete -X half, add Mirror modifier with
   clipping (the course does exactly this on Body-LOW).
4. Report poly counts before/after every step.

Manual (guide, don't fake it): topology-level cleanup. RetopoFlow's PolyPen
over the REF surface (the course's method) or Quad Remesher on thin parts
(quad count ≈ 100–300 for eyebrow/eyelash class). No paid add-ons → fallback:
Shrinkwrap + manual poly-build, or Remesh modifier + decimate.

Target guide from the course: head-class ≈ 2,500 tris LOW, whole character
LOW ≈ 12k tris. Verify with MCP poly-count reads.

## Stage 4 — UV + 텍스처 (반자동)

Goal: one atlas (`LOP_Atlas` style) shared by the LOW meshes, painted.

Automate: UV unwrap per LOW mesh (Smart UV or seams the user marks), pack all
parts into one atlas, create the paint image + material slots, set viewport to
Texture Paint-ready. Bake REF→LOW normal/AO if REF detail matters.

Manual: the actual painting (course paints flat colors + simple shading with
Paint Hard brush). Offer: generate a flat-color starter fill per part from the
reference sheet palette (sample the ref images for the palette and fill by
material region) — apply only if the user says yes.

Verify: every LOW mesh has UVs inside 0–1 and the atlas image is saved to
disk (unsaved paint dies with the session — save it for them via MCP).

## Stage 5 — 리깅 (가이드 + 대안)

Re-run the add-on audit if stage 3 skipped it — Rigify especially can be
enabled automatically. Options, in course order:

1. **Auto-Rig Pro Smart** (paid): guide the marker placement (chin, shoulders,
   wrists, spine root), Match to Rig, then bind LOW meshes.
2. **Tripo 자동 리깅**: Tripo can rig its own export — fastest for a quick pose.
3. **Rigify** (free, built-in): basic-human meta-rig, align bones via MCP where
   possible, generate rig, weight paint check.

Verify: pose a shoulder and a knee via MCP; look for mesh spikes (bad
weights); report which vertex groups need manual paint.

## Stage 6 — 씬 · 라이팅 · 렌더 (반자동)

Goal: the character posed in a scene with lights and a render.

Automate via MCP: import/build the environment, place the character, camera
framing, three-point light or the course's spark-light setup, render settings
(EEVEE for iteration), fire a render and show the file path. Iterate on the
user's art direction words ("더 따뜻하게", "역광"); each note maps to concrete
light changes you make and re-render.

---

## Ground rules

- One stage at a time; never run ahead of an unverified manual step.
- Every Tripo generation costs credits — always warn before a generate click.
- Destructive Blender edits (delete half, remesh) — snapshot first: duplicate
  the object into `REF` or save an incremental .blend via MCP.
- If the Blender MCP is not connected, stop and fix that first
  (`revvy-external-control` skill) — this pipeline without MCP is a lecture,
  not automation.
