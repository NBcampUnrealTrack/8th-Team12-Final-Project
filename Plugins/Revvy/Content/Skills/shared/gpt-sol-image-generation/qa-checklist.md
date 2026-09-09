---
description: Acceptance gates for GPT Sol and Codex-generated images before reporting success or importing them into Unreal.
---

# Image Generation QA Checklist

Evaluate the actual generated artifact. Do not pass an item based only on the generator's text response.

## 1. File Integrity

- Confirm the returned path exists and points to a newly generated file.
- For an automated Codex run, require one matching structured
  `imageGeneration` start/completion event. An assistant message, marker, path,
  or new file by itself is not tool provenance.
- Verify the event's base64 PNG hash, system-derived saved-path hash, staged
  source hash, and final output hash. Reject mismatches, symlinks, path changes,
  duplicate event keys, multiple image calls, or any shell/file fallback.
- Confirm the file decodes as the expected PNG, JPEG, or WebP format.
- Confirm dimensions and aspect ratio match the request.
- Confirm transparency is present when requested.
- Reject empty, corrupt, truncated, placeholder, or unexpectedly tiny files.

## 2. Prompt Compliance

- Confirm the requested subject, count, pose, composition, palette, and style are present.
- Confirm prohibited text, logos, watermarks, borders, or UI chrome are absent.
- Confirm the subject is not unintentionally cropped and important details stay inside safe margins.
- Confirm the background matches the intended transparent, black, flat, or scene treatment.

## 3. Visual Quality

- Check silhouette readability at the target display size.
- Check anatomy, perspective, repeated structures, and object continuity for visible defects.
- Check edges for halos, jagged alpha, accidental cutouts, and background contamination.
- Check color banding, muddy values, blown highlights, and unintended baked lighting.
- Compare against any supplied reference for identity, proportions, costume, palette, and art direction.

## 4. Unreal Texture Gates

For raw Unreal textures, also require:

- exact requested dimensions and power-of-two sizing when the target pipeline requires it;
- at least 8 percent safe padding for isolated sprites;
- pure black edge falloff for additive textures;
- full grayscale range and no color cast for masks;
- matching opposite edges for tileable noise and flow maps;
- valid tangent-space channel bias for normal maps;
- exact cell count, order, framing, and no cross-cell bleed for flipbooks;
- usage-appropriate sRGB, compression, alpha, mip, and texture-group settings after import.

Use the structured `quality` result from `generate_vfx_texture`, but still inspect the image visually. Any error-severity issue or a score below the requested threshold fails the gate.

## 5. Correction Policy

On failure:

1. Preserve the rejected artifact.
2. Convert observed defects into a short correction prompt.
3. Regenerate a general native image once. For `generate_vfx_texture`, use its bounded `attempts` input and do not wrap it in another retry loop.
4. Re-run every applicable gate.
5. Stop if the same defect remains or the capability reports another hard failure.

## Acceptance Evidence

Record the artifact path, dimensions, effective agent model/provider, image
tool event identity, source/final hashes, applicable gate results, quality
score when available, attempt count, and the reason for any accepted
exception. Do not label the outer GPT agent model as the image model.
