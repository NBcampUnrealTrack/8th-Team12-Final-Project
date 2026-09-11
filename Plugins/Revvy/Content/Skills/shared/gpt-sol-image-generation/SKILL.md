---
name: gpt-sol-image-generation
description: Generate, refine, quality-check, and optionally import images with GPT Sol or another Codex session that advertises native image generation. Use automatically for image creation or editing, concept art, Unreal textures, UI art, icons, visual references, sprites, masks, flipbooks, and requests mentioning GPT Sol, gpt-5.6-sol, image generation, 이미지 생성, 그림 생성, 컨셉 아트, or 텍스처 생성.
---

# GPT Sol Image Generation

Generate real image artifacts through an advertised image-generation capability, validate them, and preserve the user's existing assets.

## Critical Rules

1. Treat tool availability as the capability gate. A model name such as `gpt-5.6-sol` is not proof that image generation is available.
2. Use the current session's native image-generation tool for concept art, UI art, icons, illustrations, and general visual references. In a Codex/GPT Sol session, explicitly invoke `$imagegen`; do not assume ordinary text generation will create an artifact.
3. Use `generate_vfx_texture` only for raw Unreal Niagara texture inputs covered by its `usage` contract.
4. Do not substitute PIL, NumPy, SVG, procedural drawing, screenshots, or web image search for requested AI image generation unless the user explicitly accepts that alternative.
5. Never overwrite an existing source file or Unreal asset by default.
   Revvy's current Unreal texture path has no transactional backup/rollback
   contract, so automated asset replacement remains fail-closed even with
   approval; generate to a new name.
6. Inspect and quality-check the produced file before claiming success. Load `gpt-sol-image-generation/qa-checklist` for the acceptance gates.
7. Do not fabricate an output path when generation is unavailable or fails.
8. Keep model identities separate. `gpt-5.6-sol` identifies the outer agent
   turn; it is not the image model. Require a real native tool event before
   attributing an artifact to image generation.

## Capability Routing

| Request | Required capability | Route |
|---|---|---|
| Concept art, UI art, icon, illustration, reference image | Native image-generation tool advertised in the current session | Generate directly with that tool |
| Niagara sprite, additive texture, mask, normal, flow map, or flipbook | `generate_vfx_texture` | Generate, pixel-QA, and import when Unreal is online through the typed VFX texture workflow |
| Capture or inspect the Unreal Editor | Screenshot tools | Load `screenshots`; do not generate a replacement image |
| Import an accepted image into Unreal | `manage_asset` import support and a connected editor | Validate the destination, import, configure, and re-read the asset |

If the required capability is absent, stop without changing files and state exactly which capability is missing.

## Workflow

### 1. Establish the Output Contract

Determine or reasonably infer:

- intended use and subject;
- style and visual references;
- dimensions or aspect ratio;
- transparent, black, white, or scene background;
- whether text is allowed;
- source-file destination and Unreal destination, when applicable.

Search before creating an Unreal asset. If the requested destination already exists, choose a new name unless the user explicitly approves replacement.

### 2. Build a Production Prompt

State the asset's purpose, composition, subject, style, palette, background, edge treatment, and prohibited artifacts. For production source art, explicitly reject unintended text, logos, watermarks, frames, and cropping.

For a correction attempt, describe only the observed defects and the required correction. Do not silently change the approved art direction.

### 3. Generate Through the Correct Capability

For general images, explicitly call `$imagegen` (or the equivalent native image tool advertised by a non-Codex client) once and retain its returned artifact path.

For Niagara textures, call `generate_vfx_texture` with:

- required `subject`;
- required supported `usage`: `sprite`, `additive`, `mask`, `tileable_noise`, `flowmap`, `normal`, or `flipbook`;
- required `destination_path` as a `/Game/...` folder and required `asset_name` as a `T_...` name;
- optional `style` and `size`;
- `grid_columns` and `grid_rows` for a flipbook;
- `quality_threshold` and bounded `attempts`;
- optional `model` only when the current Codex model is explicitly confirmed as GPT Sol; pass the exact advertised slug such as `gpt-5.6-sol`, otherwise omit it;
- `replace_existing:false`; the current typed Unreal import path rejects
  replacement because it cannot roll back a partially configured asset.

Read `success`, `source_png`, `quality`, `attempts`, `attempt_history`, `asset_path`, and `imported`. A generated file that failed its quality gate is not a successful result. When Unreal is offline, the tool may return a staged, QA-passed source with `imported:false`; report it as staged, not imported.

Do not infer or rewrite the model slug. `model` is a routing hint for the
isolated Codex agent turn, not evidence that the selected session has image
capability and not the image-model identity. For Revvy's automated runtime,
require Codex app-server JSONL provenance: one matching
`imageGeneration` started/completed pair, the effective agent model/provider,
the system-derived saved path, and matching tool-result/source/output hashes.
Reject shell, file, web, MCP, or procedural fallback items. `codex exec --json`
does not expose the image-generation item and is not sufficient provenance.

### 4. Run QA

Load and follow `gpt-sol-image-generation/qa-checklist`. Inspect the actual output rather than trusting the generation message.

For a general native image, allow at most one focused correction cycle after the initial result. For `generate_vfx_texture`, let its bounded `attempts` policy apply QA feedback internally and do not add a blind outer retry. Preserve rejected artifacts when the capability exposes them and record the reason for regeneration. Stop after repeated unchanged defects.

### 5. Import Only After Acceptance

For a general image generated through the native image tool, when Unreal import is requested:

1. Confirm the editor is connected.
2. Confirm the exact `/Game/...` destination does not already exist.
3. Import the accepted source file with `manage_asset`.
4. Apply usage-appropriate color-space, compression, alpha, and texture-group settings.
5. Save and re-read the imported asset to verify path, dimensions, and settings.

Do not present a staged local PNG as an imported Unreal asset.

`generate_vfx_texture` performs its own validated import after pixel QA when Unreal is online. Do not import the same output again. Require `imported:true`, then re-read the returned `asset_path` and verify its settings before claiming the Unreal asset is complete.

## Final Evidence

Report:

- the generated source path;
- dimensions and format;
- the effective agent model separately from the image tool/model;
- structured tool-event and artifact-hash provenance for automated runs;
- QA result and any correction attempts;
- imported Unreal asset path and verified settings, if imported;
- any remaining limitation or manual review item.

## COMMON_MISTAKES

- Assuming GPT Sol always exposes image generation instead of checking the current tool set.
- Calling `generate_vfx_texture` for concept art or UI illustrations.
- Treating a screenshot, procedural drawing, or searched image as generated output.
- Importing before checking cropping, alpha, seams, grid layout, or prompt compliance.
- Retrying blindly without using the prior QA issues as a correction prompt.
- Overwriting an existing file or Unreal asset without explicit approval.
