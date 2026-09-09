---
name: effect-generation
description: Design, build, validate, and optimize production Unreal Engine VFX with Niagara, materials, textures, timing, and game-feel layers. Use for new effects, effect reconstruction, impact polish, emitter recipes, reusable material functions, Niagara diagnostics, or VFX performance work.
---

# Effect Generation

Load only the sub-document needed for the current step. Do not load the whole knowledge base.

## Route the Task

- New tutorial-free effect: `effect-generation/AESTHETIC_GRAMMAR`, then the matching `effect-generation/ARCHETYPE_PROFILES`, then `effect-generation/PRODUCTION_PIPELINE`
- Reconstructed or shipping effect: `effect-generation/PRODUCTION_PIPELINE` first; it owns the Effect IR and completion gates
- "Ugly", generic, flat, or visually mismatched effect: `effect-generation/VISUAL_VALIDATION`, `effect-generation/FAILURE_ATLAS`, `effect-generation/DETAIL_POLISH`, then `effect-generation/CRITIQUE_AND_REPAIR`
- Fast assembly from existing emitter blocks: `effect-generation/FASTBUILD`
- Known effect recipe: `effect-generation/RECIPE_LIBRARY`
- General build recipe: `effect-generation/RECIPES`
- Niagara version-specific modules and pins: inspect the running engine version first, then load `effect-generation/NIAGARA_5_7` only when it matches
- Material functions: `effect-generation/MATERIAL_FUNCTIONS`
- Multi-stage timing: `effect-generation/SEQUENCING`
- Impact feel: `effect-generation/FEEL`
- CGHOW-derived buildable tutorial studies: `effect-generation/CGHOW_TUTORIAL_STUDIES`
- Profiling and budgets: `effect-generation/PERF`
- Full document map: `effect-generation/INDEX`

## Production Entry Points

- GPT Image texture generation and import: external MCP tool `generate_vfx_texture` with `subject`, `usage`, `/Game/...` `destination_path`, and `T_...` `asset_name`.
- Polished template-first preset: `levy_vfx.build_production_preset("combat_impact_polished", path, overrides={"quality_threshold": 82})`; polished presets are `combat_impact_polished` and `arcane_burst_polished`, while `combat_impact` and `arcane_burst` remain baseline compatibility presets.
- CGHOW study collection: `levy_vfx.cghow_tutorial_catalog()`, then `levy_vfx.build_cghow_tutorial("cghow_study_impact")`; use `finalize_cghow_tutorial` on the next editor tick when the first graph-changing build reports `simulation_failed`.
- If a graph-changing build reports `simulation_failed`, call `levy_vfx.finalize_production_preset("combat_impact_polished", path)` in the next Unreal tool invocation; it retries startup compilation, runs Sim Cache evidence, and saves only on pass.
- Safe GeometryScript recipe: `levy_vfx.build_procedural_mesh({"path": "/Game/VFX/_RevvyStaging/Meshes/SM_Ring", "parts": [{"type": "torus"}]})`.
- JSON structural audit: `json.loads(levy_vfx.audit_system_json(path, phases=["anticipation", "emission", "body", "impact", "decay"], profile="combat", threshold=82))`.

Load `effect-generation/PRODUCTION_PIPELINE` for complete argument and return-value examples. A passing structural audit does not replace runtime, pixel, aesthetic, archetype, or PIE performance evidence.

Generic `MI_RevvyTutorial_*` materials and tutorial study presets are prototype-only. They may prove graph, timing, and runtime viability, but production candidates require an art-direction-specific material/texture/mesh contract and all visual gates.

## Tutorial-Free Quality Loop

Run new-effect work as one closed loop:

`intent -> aesthetic IR -> three constrained candidates -> deterministic validation capture -> structural/runtime/pixel/aesthetic/archetype metrics and critique -> one causal repair -> recapture -> explicit approval`

Only an explicitly approved candidate becomes a positive exemplar. Tutorial emitter order, unapproved output, and structural score alone are never learned as production style.

## Build Loop

1. Query the running engine version, existing system path, emitters, renderers, materials, and user parameters.
2. Write the machine-readable aesthetic IR from `AESTHETIC_GRAMMAR`, select an archetype profile, and define one Primary, supporting Secondary layers, palette roles, shared motion field, and forms to avoid.
3. Use template-first discovery: reuse a verified project system/emitter and preserve its renderer, curves, alignment, and material contract before creating another asset.
4. Discover exact live service signatures and valid enum or module values.
5. Write the `PRODUCTION_PIPELINE` Effect IR, stagger anticipation/Primary/follow-through/decay, then build up to three constrained staging candidates with stable layer IDs.
6. Verify every transient layer has a curve or `ParticleRelativeTime` material fade contract; directional sprites must use the intended alignment.
7. Compile the material and Niagara system before opening either editor.
8. Read diagnostics, capture candidates in the validation level, pass the five visual gates, make causal repairs one axis at a time, then profile target quality and worst-case instances.

## Safety

- Never guess a Niagara module name, pin, enum value, emitter index, asset path, or engine-version API.
- Do not open a partially assembled Niagara system when diagnostics report invalid emitter or renderer indices.
- Do not delete or replace an existing system, emitter, material, or texture without explicit confirmation.
- Keep the original asset unchanged when reconstructing an effect; duplicate to a verified destination first.
- Confirm before changing project rendering settings or applying work to 100 or more placed components.

## Completion Evidence

Require all of the following before reporting completion:

- exact system and material paths;
- successful material and Niagara compilation;
- no blocking diagnostics;
- verified emitter, renderer, and parameter counts;
- viewport or PIE observation of timing and framing;
- representative visual captures on dark/light backgrounds at gameplay scale;
- separately recorded structural, runtime, pixel, aesthetic, and archetype gate results;
- a passing aesthetic gate for clear Primary/Secondary hierarchy, staggered timing, motion variety, and temporal decay without hard pops;
- an explicit candidate selection/approval record; regression pass alone is not initial art approval;
- source paths for reused template emitters and the renderer/material contracts preserved or intentionally changed;
- Effect Type, quality tier, simulation mode, and bounds evidence;
- performance evidence appropriate to the requested platform.
