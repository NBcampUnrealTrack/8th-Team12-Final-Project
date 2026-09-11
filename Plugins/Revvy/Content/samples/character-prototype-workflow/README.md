# Solar Courier: GPT Concept to Blender Blockout

This sample exercises Revvy's capability-gated image and Blender workflows with
one reproducible low-poly character prototype. GPT creates a visual concept,
and Blender MCP assembles a static multi-part blockout from the exact numeric
contract in `design-spec.json`.

The JSON specification is authoritative for geometry. The generated image is
an art-direction reference, not an input to automatic image-to-mesh
reconstruction.

## Prototype Contract

- Character: **Solar Courier**, a chunky low-poly sci-fi courier.
- Concept artifact: one opaque `1024x1024` PNG.
- Modeling unit: one Blender unit is treated as one meter.
- Coordinate system: Blender Z-up; the character faces `-Y`.
- Side convention: `_L` is negative X and `_R` is positive X.
- Geometry source: the exact factory-startup object named `Cube`.
- Deliverable: a static, disconnected, multi-part GLB blockout.
- Source preservation: `Cube` and its original mesh fingerprint must remain
  unchanged.

Every material ID and part ID in `design-spec.json` is exact, unique, and
matches `^[A-Za-z0-9_]+$`. Do not normalize, translate, or guess an ID.

## Required Capabilities

Load `gpt-sol-image-generation` and its QA checklist for the concept stage.
Load `blender-3d-modeling` for the blockout stage.

The Blender MCP catalog must advertise all of these tools before any mutation:

- `blender_bridge_status`
- `blender_job_status`
- `blender_scene_inspect`
- `blender_mesh_inspect`
- `blender_mesh_edit`
- `blender_modifier_apply`
- `blender_material_assign`
- `blender_uv_inspect`
- `blender_uv_edit`
- `blender_model_export`
- `blender_model_import`
- `blender_save_copy`

If native image generation or any required Blender tool is absent, stop before
changing the scene. Do not substitute procedural drawing, arbitrary Blender
Python, shell commands, console commands, or UI macros.

## Reference Commands

Use a fresh output directory for each run. The concept-stage launcher reads the
exact prompt and size from this sample's `design-spec.json`; an unrelated prompt
or size override is refused.

```powershell
$run = "Saved/Revvy/WorkflowTests/SolarCourier-<new-run-id>"
python Tests/Python/run_character_concept_stage.py `
  --spec Content/samples/character-prototype-workflow/design-spec.json `
  --output "$run/concept.png" `
  --result "$run/generation-result.json" `
  --model gpt-5.6-sol
```

After inspecting `concept.png`, write the structured
`revvy.character_visual_review.v1` JSON described below. Then run the typed
Blender MCP stage:

```powershell
python Tests/Python/blender_character_prototype_smoke.py `
  --spec Content/samples/character-prototype-workflow/design-spec.json `
  --concept "$run/concept.png" `
  --concept-origin gpt_imagegen `
  --generation-evidence "$run/generation-result.json" `
  --visual-review-evidence "$run/visual-review.json" `
  --output-dir "$run/Blender"
```

## Stage 1: Generate and Approve the Concept

Use the exact `concept.prompt`, `concept.size`, `concept.format`, and
`concept.background` values from `design-spec.json`. Generate exactly one new
PNG, for example:

```text
Saved/Revvy/WorkflowTests/<run-id>/concept.png
Saved/Revvy/WorkflowTests/<run-id>/generation-result.json
Saved/Revvy/WorkflowTests/<run-id>/visual-review.json
```

Do not replace an existing file. Record the prompt hash and the generated PNG
SHA-256 in create-new generation evidence. The evidence must record
schema `revvy.character_generation_evidence.v2`, generator
`Tests/Python/run_character_concept_stage.py`, `success:true`, model
`gpt-5.6-sol`, capability
`codex-app-server:item/completed:imageGeneration`, exact prompt, an attempt
count of one or two, and an artifact object containing the absolute PNG path,
SHA-256, byte count, format, width, and height.

Use Codex app-server JSONL for provenance. `codex exec --json` does not expose
the built-in image-generation item and is not sufficient evidence. Require one
`imageGeneration` `in_progress -> completed` pair with the same item, thread,
and turn IDs; bind `clientUserMessageId` to the invocation marker; require the
system-derived `savedPath`; and verify the decoded tool-result SHA-256 against
both the saved source and final promoted PNG. Reject shell, file, MCP, web, or
procedural fallback items even if they leave a valid-looking PNG.

Report the model roles separately: `gpt-5.6-sol` is the outer agent model
selected for the turn. The image-generation tool chooses its own GPT Image
model, and the current per-call app-server item does not expose that model
field. Record `image_model: null` and `image_model_attested: false`; do not
present any GPT Image model name as a per-artifact attestation. A two-attempt
result must bind one preserved rejected PNG and its reason.

Inspect the actual image and write a separate create-new
`revvy.character_visual_review.v1` checklist bound to the accepted PNG hash.
Every semantic check below must be explicitly true. This is a structured
human-or-vision-review attestation; numeric PNG QA alone does not infer
character semantics. The evidence binds the submitted review to the PNG hash,
but it does not authenticate the reviewer identity:

- a decodable PNG with exact `1024x1024` dimensions;
- exactly one complete, centered, full-body character;
- helmet, visor, chest panel, cargo pack, two solar cells, paired arms, paired
  legs, and both boots visible;
- at least 12 percent visual breathing room around the silhouette;
- the navy, teal, orange, gold, graphite, and ash-gray palette;
- a plain opaque light-gray background;
- no crop, text, letters, numbers, logo, watermark, border, frame, weapon,
  scenery, extra character, extra limb, or missing limb.

Allow at most one focused correction attempt. Preserve a rejected image and
record its hash and rejection reason. Bind approval to the exact accepted PNG
SHA-256 so a later file cannot silently replace it.

The Blender runner refuses an end-to-end claim unless
`--generation-evidence` and `--visual-review-evidence` both validate against
the exact accepted PNG. Generator evidence is hash-bound local process
evidence, not a cryptographic attestation.

Concept generation is stochastic. Reproducibility means preserving the exact
prompt, model/capability identity, image hash, and accepted artifact. It does
not mean that a second generation will produce identical pixels.

## Stage 2: Establish the Blender Baseline

1. Call `blender_bridge_status` and confirm arbitrary code execution is false.
2. Call `blender_scene_inspect` and resolve the exact mesh object `Cube`.
3. Call `blender_mesh_inspect` and record the source fingerprint and geometry.
4. Save a new backup such as `SolarCourier_baseline.blend` with
   `overwrite:false`.
5. Stop if `Cube` is missing, ambiguous, already changed, or the backup path
   exists.

The sample assumes Blender's factory-startup cube is exactly two units on each
axis with applied scale. For each part, calculate the vertex scale as:

```text
vertex_scale = dimensions / [2.0, 2.0, 2.0]
```

Do not create or replace the source cube to repair a failed precondition.

## Stage 3: Build Every Cuboid Through Typed MCP

Process `parts` in JSON order. Use exact output names derived without
normalizing the part ID:

```text
SC_<PartId>_Shape
SC_<PartId>_Bevel
SC_<PartId>_Material
SC_<PartId>_Final
```

For each part:

1. Call `blender_mesh_edit` from the original `Cube` to
   `SC_<PartId>_Shape`.
2. In that call, apply `transform_vertices` with `dimensions / 2`, then
   `transform_object` with the exact `location` and `rotation_degrees`, object
   scale `[1, 1, 1]`, followed by `recalculate_normals` and
   `shade_smooth:true`.
3. Call `blender_modifier_apply` to copy the shape to
   `SC_<PartId>_Bevel`. Use a `bevel` modifier with the exact
   `bevel_width`, exact `bevel_segments`, and `angle_degrees:30`.
4. Call `blender_material_assign` to copy it to
   `SC_<PartId>_Material`. Resolve `material_id` to the exact material object
   in the JSON. Create that material only on its first use, using its exact
   name and Principled values; reuse the exact existing material afterward.
   Replace inherited factory material slot `0` instead of appending a second
   unintended material slot.
5. Call `blender_uv_edit` with `smart_project` to create
   `SC_<PartId>_Final`, using UV layer `SolarCourierUV`,
   `create_layer:true`, `angle_limit_degrees:66`, and
   `island_margin:0.03`.

Each mutation must use a unique stable request/job ID. If acknowledgement is
lost or a call times out, poll `blender_job_status`; never submit the mutation
again. After every terminal mutation, call `blender_mesh_inspect` on the exact
mutation source, output, and original `Cube`; compare each returned fingerprint
to the mutation receipt and baseline before starting the next mutation.

## Stage 4: QA and Export

For every `SC_<PartId>_Final`, call both mesh and UV inspection. Require:

- the material and UV gates from `qa` to pass;
- manifold geometry when `require_manifold` is true;
- applied object scale when `require_applied_scale` is true;
- zero non-finite vertices, zero-length edges, loose vertices, and degenerate
  faces;
- finite transforms, bounds, and UV coordinates;
- the exact intended material name;
- dimensions, location, and rotation matching the JSON within a small
  floating-point tolerance;
- no mutation of the original `Cube` fingerprint.

Sum the final-part triangle counts and require the total to be no greater than
`qa.max_total_triangles`.

Export only the exact `SC_<PartId>_Final` object list to new
`SC_SolarCourier_Blockout.glb` and `SC_SolarCourier_Blockout.fbx` files with
`overwrite:false`. Re-import the GLB with the exact `SCQA` prefix, verify the
one-to-one source/imported name mapping, per-part material identity, per-part
triangle count, imported location, applied scale, full world matrix,
world-space bounds, and all imported mesh reports, and confirm both exported
files are non-empty. GLB may
split vertices along UV or normal seams, so record
re-imported boundary edges as a transport warning; require manifold geometry
on the authoritative pre-export Blender objects, while still rejecting
re-imported wire edges, true non-manifold edges, loose geometry, degenerate
faces, invalid normals, or non-finite vertices. Finally save new
`SC_SolarCourier_Blockout.blend` and `SC_SolarCourier_QA.blend` copies.

A complete run report should contain:

- generation-evidence path/hash/model/capability, concept prompt hash, PNG
  input/bundled-copy path/hash/dimensions, and hash-bound structured visual QA;
- Blender/add-on versions and the complete advertised tool-name list;
- backup and final `.blend` paths;
- source fingerprint before and after;
- a hash-bound JSONL request audit containing every MCP request ID, method,
  tool name, and response state, plus terminal Blender job state and output;
- per-part geometry, material, explicit transform, and UV evidence;
- total triangles and the configured limit;
- GLB/FBX path, byte size, SHA-256, and GLB re-import name/material/triangle
  evidence;
- all warnings, rejected artifacts, and manual-review items.

`--concept-origin qa_fixture` exists only for isolated Blender component
testing. It sets `end_to_end:false`, cannot be combined with generation
or visual-review evidence, and must never be described as a generated or
visually approved character concept. The runner copies the fixture into the
new output directory so the final evidence bundle does not depend on an older
run directory.

## Typed MCP Limits

This sample deliberately stays inside the current closed Blender schemas:

- It does not reconstruct geometry from pixels. The image and blockout share
  an art direction, while `design-spec.json` drives the actual geometry.
- Typed MCP currently has no primitive-create or reference-image-plane tool,
  so the sample requires the existing factory `Cube`.
- It has no mesh join, Boolean authoring, sculpting, retopology, texture-image
  assignment, rig creation, skin weighting, or character animation workflow.
- It has no typed render or viewport-capture tool. Structured geometry QA does
  not replace a final human visual review in Blender or a GLB viewer.
- Material assignment covers bounded Principled values, not applying the
  concept PNG as a texture.
- Non-destructive typed operations leave intermediate shape, bevel, and
  material copies in the `.blend`. Export only the explicit `_Final` objects.

The result must therefore be described as a static multi-part prototype
blockout. It is not watertight, rigged, animated, textured, retopologized, or
game-ready. Never use arbitrary code as a fallback to make it appear otherwise.
