# Detail Polish

Use this pass after structural and runtime validation. More emitters do not imply a more finished effect; every visible layer must strengthen the subject, its cause, or its contact with the environment.

## Causal Bindings

Define three anchors before adding detail:

- source: where energy, fluid, or mass begins;
- target: where its motion is aimed or constrained;
- contact: where it meets a surface, object, or boundary.

Bind secondary motion to one of those anchors. Sparks without a contact, mist without a fluid body, and arcs without endpoints read as decorative noise.

## Spatial Frequency Roles

Build a deliberate frequency hierarchy:

- hard core: the crisp, high-contrast gameplay silhouette;
- body: medium-scale flow that explains volume and direction;
- halo: low-contrast smoke, mist, glow, or residue.

The core owns the brightest value and cleanest edge. Body layers may overlap it briefly but must not replace its silhouette. Halo layers stay broader, slower, and dimmer. Remove any layer whose removal improves readability.

## Archetype Constraints

- Impact: use a directional wedge, compression line, or contact slash. Full radial rings and centered starbursts are accents, not the primary shape.
- Portal: establish void, boundary, front/back flow, and traversal depth. A bright flat disc is incomplete even when decorated.
- Lightning: keep one anchored hero bolt, a compact contact flash, and a branch hierarchy. Branches are thinner and dimmer; endpoints must remain stable.
- Water: connect crown, sheet, droplets, mist, and surface response through one fluid event. Surface optics and wet contact are mandatory.
- Tornado: preserve one continuous tapered macro body. Height-banded smoke and sparse debris describe rotation; stacked rings never define the funnel.

## Generated Asset Gate

Do not accept a generic star, circle, or ring texture when the effect requires a gameplay silhouette. Inspect the source image for padding, alpha/black-level behavior, asymmetry, and directional readability. Then capture the material inside Unreal at gameplay scale; a source-image quality score is evidence, not visual approval.

## Repair Loop

1. Capture the effect and a blank frame with a fixed camera, background, exposure, and simulation age.
   Enumerate every Niagara component in the level first, including preview actors and Sim Cache components attached to utility actors; hide all non-target components before both frames.
   Capture the blank in the same editor session and exposure state. Use an effect-local ROI when real light spill changes the floor outside the authored silhouette.
2. Name the single dominant visual failure: silhouette, hierarchy, contact, depth, palette, motion, or residue.
3. Change one causal layer or material contract.
4. Recompile, recapture, and compare against the previous candidate.
5. Keep the change only when both readability and archetype identity improve.

Automated pixel and structural reports detect regressions; they do not approve beauty. Approval still requires a deterministic Unreal capture and explicit visual review.
