## Multi-Stage Effect Sequencing

> How 레비 composes a polished effect from staged parts. Every effect = three stages on one master timeline: **Anticipation → Impact/Burst → Linger/Fade**. Drive everything from a single Timeline float (0.0–1.0) so material, color, camera shake, and sound never drift apart.

---

### Stage Timing (universal skeleton)

| Stage | Duration | Particles | Emit rate | Velocity | Scale | Emissive |
|---|---|---|---|---|---|---|
| **Anticipation** | 40–80ms | 2–8 | 10–20 p/s | 100–300 UU/s up | 0.3 → 0.7 | 0.3–0.5 |
| **Burst/Impact** | 80–200ms | 50–200 | 500–2000 p/s (spike only) | 800–1500 UU/s radial | 1.0–1.5 peak | 0.9–1.0 |
| **Linger/Fade** | 200–600ms | decay → 0 | 200 → 0 p/s (exp) | 10–50 UU/s | settle, alpha 1→0 | 0.1–0.3 |

- Total lifecycle: 400–800ms (horror impact); action 800–1400ms
- Inter-stage gap: 0–50ms (events fire same frame; handlers run next frame)

---

### Master Timeline (one float drives all)

Single float track 0.0–1.0 keyed:

| Time | Float | Drives |
|---|---|---|
| T=0 | 0.0 | spawn anticipation emitter |
| T=0.05s | 0.7 | anticipation ramp |
| T=0.12s | 1.0 | **burst peak** — emissive max, camera shake, impact sound |
| T=0.35s | 0.5 | mid-linger |
| T=0.70s | 0.0 | final fade, deactivate system |

Wire the float output through Lerp nodes into:
1. **Material Scalar** `EmissiveIntensity` → `[Float × 2.0]` → Emissive multiplier (smooth ramp, no new emitters)
2. **Niagara user params** (spawn rate override, color multiplier) via `Set Niagara Variable` each frame
3. **Camera shake amplitude** → `[Float × 8.0]` → 0–8.0 amplitude

---

### Niagara Lifecycle Execution Order (sequence sub-emitters correctly)

```
System Spawn (frame 0, once)
  → System Update (every frame)
    → Emitter Spawn (frame N when emitter enabled)
      → Emitter Update (every frame)
        → Particle Spawn (when born)
          → Particle Update (each frame alive)
            → Event Handler stages (same frame as event, but AFTER)
```

- Put ALL stage spawn logic in **Emitter Spawn** group (frame 0 when enabled), NOT Particle Spawn — guarantees synchronized stage transitions.
- Stage emitters into **separate Niagara emitters** (anticipation / burst / linger) for independent lifetime tuning + boolean control.
- Do NOT nest systems (system spawning system) for cascades — use event handlers within one system.

---

### Modular `use*` Toggles

Create booleans in the system, bound to each emitter's `Enabled` condition in Emitter Spawn:

- `bEnableAnticipation`
- `bEnableMainBurst`
- `bEnableLinger`
- `bEnableCollisionSpawns`
- `bEnableGlowRing`
- `bEnableShockwave`

Set from Blueprint via `Set Niagara Variable` **before** `Activate()` (never mid-effect). Presets:
- `BURST_ONLY` = anticipation false, burst true, linger true
- `FULL_SEQUENCE` = all true
- Fast stab vs slow charge = same system, just toggle anticipation duration (40ms vs 200ms) + burst peak time, reuse linger.

---

### MPC Color Sequencing

- Use **Material Vector Parameters** (MPC or dynamic) for color tints — NOT per-particle user params (much faster).
- Niagara Vector3 user param `ColorTint`, driven from a Blueprint color track (or 3 float tracks R/G/B):
  - Horror: red (1.0,0.2,0.2) @T=0 → white/yellow (1.0,0.9,0.3) @T=0.12s burst → dark purple (0.4,0.1,0.3) @T=0.4s
- Plug timeline output → `Set Niagara Variable` looped every frame.

---

### Camera Shake + Sound Orchestration

**Camera shake** (Timeline event track @T=0.12s = burst peak):
- `Play Camera Shake` — Amplitude 6.0, Frequency 22 Hz, Duration 0.12s, fade-out 50ms
- Or parameterize amplitude (0–8.0) from the master float, keyed to emissive

**Sound** (layer for tension):
- Impact sound @T=0–5ms (0ms latency, frame 0 of burst)
- Tail/aftershock sound @T=150–200ms (linger start)
- Tie to character actions via Anim Notifies; otherwise Blueprint delay sequence

---

### Event-Driven Sub-Emitters

**Collision spawns:**
- In burst emitter, add `Generate Collision Event` (Particle Update); threshold: impact velocity > 500 UU/s
- Event Handler in linger emitter: on collision, spawn 8–15 sparks (lifetime 0.3–0.5s, inherited velocity + 200 UU/s radial)

**Death spawns (micro-pop):**
- `Generate Death Event` on burst particles; when they die (~0.8s), spawn 3–5 micro-puff particles (scale 0.2–0.4, 0.1s fade)
- Reuse one small death emitter across all parent systems

**MANDATORY for events:**
- Enable **Requires Persistent IDs** on emitters using events (or it silently does nothing)
- **CPU simulation only** — GPU sim + events = silently non-functional
- Gate stage transitions with explicit delay/timeline logic, NOT just event handler timing (events fire same frame, handlers run after → pop-in risk)

---

### Concrete Example A — Horror Death Effect

Palette: cool desaturated, emissive <0.5, Translucent, CPU. `BP_HorrorDeath` wraps system + timeline + camera shake + sound.

```
T=0     Spawn system @ death location (bEnableAnticipation/Burst/Linger = true)
        Anticipation: 4–6 dust sprites, emissive 0.4, color #2D5A6C,
        upward drift 200 UU/s, scale 0.3→0.7
T=0.05  Float 0.7 — fog density ramps via Curl Noise drift 15 cm/s
T=0.12  BURST: 40–50 blood particles (#5C2E2E), gravity 0.4, lifetime 2s,
        emissive 0.2 (wet, no glow). Camera shake AMP 3.0, 18Hz, 0.10s.
        Wet impact sound @0ms. NO bloom spike (stays <0.35).
T=0.35  Collision event on blood hitting floor → spawn decal splat (not particles)
        Linger fog: emissive 0.3→0.1, desaturate to ash/gray
T=0.70  Alpha fade to 0.1–0.2 lingering haze; particles persist as dread
T=2.0+  Fog particles slow-die (lifetime 2–4s), emitters cull at 5000 units
```
Total particles <300, CPU, ~0.5–1.2ms/frame.

---

### Concrete Example B — Action Hit (Melee)

Palette: orange/red HDR 8.0–10.0, sat 0.9+, bloom-bright core. `BP_MeleeHit` wraps system + timeline + hit-stop + screen-shake + sound, all synced frame 0.

```
T=0      Anticipation (200–300ms pre-impact in the swing): inward pull
         scale 0.3–0.5, vertical squash, emissive +50% (4.0→6.0)
T=impact BURST @ frame 0 (80ms impact window):
         - Niagara spawn + material emissive peak 8.0–12.0 + screen-shake
           ALL fire t=0 (async <16ms)
         - Core: 2×2 px cluster @10.0 brightness, pure orange (no gray)
         - Scale: 0→1.6 in 80ms, overshoot 2.0–2.2x @frame 4–6
         - HIT-STOP 120ms (input + NPC freeze)
         - Screen-shake 4 px offset, 3 frames
         - Impact sound @0ms
T=+0ms   Secondary: burst 8–15 sparks, 60% fall (gravity 600 UU/s²),
         40% radial (500 UU/s), cone 35°, yellow (contrast to core),
         ±12% speed variation, lifetime ~900ms
T=+80ms  Scale settles to 1.0x by frame 12–15 (cubic ease-out)
T=+400ms Dissipation: emissive 8.0→0.5, scale 1.5→0.0 ease-out cubic
         (emissive fades BEFORE geometry disappears)
T=+900ms Effect complete (melee total ~900ms; heavy 1300ms)
```
Color-code variants: ranged = cyan/yellow HDR 6.0–8.0; heavy/boss = magenta/gold HDR 10.0–14.0 + hit-stop 200–300ms.

---

### Sequencing Do / Don't

- DO stage into separate emitters for modular boolean + independent lifetime control
- DO use ONE timeline float as master curve for color, emissive, shake (no drift)
- DO put stage spawn logic in Emitter Spawn group (frame 0)
- DO enable Persistent IDs + CPU sim for any event handlers
- DO trigger camera shake at burst peak with amplitude from the float
- DO layer sound: impact @0ms, tail @150–200ms
- DO use Material Vector params for color, not per-particle
- DON'T use GPU sim with collision/death/location events (silently breaks)
- DON'T nest systems for cascades — use event handlers in one system
- DON'T exceed 300 particles per multi-stage effect (tight budget for simultaneous FX)
- DON'T forget Persistent IDs (effect silently does nothing)
- DON'T rely on event timing alone for stage transitions — add explicit delay/gate
- DON'T animate emissive >1.0 without bloom enabled (flat for action)
- DON'T exceed 2 texture lookups per particle (perf-critical)
- DON'T change user params every frame for frame-perfect sync — use Timeline
