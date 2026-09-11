## VFX Performance Validation (measure → diagnose → fix)

Run this checklist for **every new Niagara effect** before merging. All commands go in the PIE console (` ~ `) unless noted.

---

### 1. Measure — exact commands

**Step 1: Find the bottleneck (always first)**
```
stat unit
```
Read: `Frame` / `Game` / `Draw` / `GPU` in ms. Highest value = where the problem lives.

**Step 2: Baseline vs. effect cost (A/B diff)**
```
stat gpu                  // per-pass GPU ms (watch Translucency)
stat niagara              // Niagara totals (GT / RT)
ProfileGPU                // captures ~10 frames, per-draw-call GPU tree (log in Saved/Profiling/)
```
Measure with effect **inactive**, then **active**. The difference is the effect's cost.

**Step 3: Isolate sim cost vs. draw cost**
```
fx.freezeparticlesimulation 1    // freeze sim, keep visuals  → remaining delta = draw cost
showflag.Particles 0             // hide particles            → remaining delta = sim cost
showflag.Translucency 0          // hide translucency         → isolates overdraw cost
```

**Step 4: Niagara Debugger (Editor: `Window > Niagara Debugger`, PIE only)**
```
fx.Niagara.Debug.Hud Enabled=1   // HUD: system count, particle count, memory, GT/RT µs
fx.budget.enabled 1              // activate global FX budget tracking
```
Check per-emitter particle counts, GT cost (left) vs RT cost (right), Show Bounds for bounds sizing, and which systems get culled under budget pressure.

**Step 5: Unreal Insights (for per-system GT timing)**

Launch with `-StatNamedEvents -trace=cpu,gpu,frame`, then in the Timing Panel filter for:
- `Niagara Manager Tick [GT]` — total tick overhead
- `System Simulation Tick [GT]` — per-system parameter prep (golden number target below)
- `Emitter Spawn [CNC]` / `Emitter Simulate [CNC]` — concurrent sim cost
- `GPU Emitter Dispatch [RT]` — render-thread dispatch

Average over 100+ frames, not single spikes.

**Step 6: Visual inspection (viewport view modes)**
- `View Mode > Optimization Viewmodes > Quad Overdraw` (Alt+8) — bright/red = translucency stacking
- `View Mode > Optimization Viewmodes > Shader Complexity` — red/white = expensive particle material

**Step 7 (optional): Long capture for variance/spikes**
```
CsvProfile Start    ... play the scene ...    CsvProfile Stop
```

---

### 2. Pass/fail thresholds (golden numbers)

| Metric | PASS | FAIL → go to §3 |
|---|---|---|
| `System Simulation Tick [GT]` per system | **< 1 ms** (ideally < 0.5 ms) | ≥ 1 ms |
| Total VFX game-thread cost (`stat niagara` GT) | **< 2 ms** | ≥ 2 ms |
| GPU cost of all active VFX (`stat gpu` / ProfileGPU diff) | **< 5 ms** | ≥ 5 ms |
| Frame total (`stat unit`, 60 fps) | Frame/Game/Draw/GPU all < 16.67 ms | any ≥ 16.67 ms |
| CPU emitter particle count | **< 2,000** per emitter (< 10k absolute) | above → switch to GPU sim |
| GPU emitter particle count | **< 50k** per system (< 100k absolute) | above → cut counts / LOD |
| Emitters per system | **< 5** (each adds ~0.05–0.1 ms GT tick) | ≥ 5 → consolidate |
| Particle material instructions | **< 100** (target < 30 for high-coverage sprites) | ≥ 100 → simplify material |
| Quad Overdraw | **< 4×** average; effect fills < 40% screen at gameplay distance | > 8× (bright red) → overdraw fix |
| Translucency cost (ProfileGPU) | simple VFX < 1 ms; smoke/explosions 1–3 ms | > 3 ms → overdraw fix |
| Memory (Debugger HUD) | < 2 MB per emitter; < 20 MB for 10 simultaneous effects | above → consolidate/LOD |

Simulation-target rule of thumb: **< 100 particles → CPU emitter; 100–1,000 → profile both; > 1,000 → GPU emitter.**

---

### 3. Diagnose — symptom → lever table

| Symptom (from §1 measurements) | Likely cause | Lever (→ §4 fix #) |
|---|---|---|
| GPU spike + large sprites + red Quad Overdraw | Translucent overdraw | Shrink sprites, alpha-erosion cutout textures (#1, #4) |
| GPU spike + red Shader Complexity on particles | Expensive particle material | Cut instructions, bake noise to textures (#2) |
| GPU spike persists with `fx.freezeparticlesimulation 1` | Draw/fill cost, not sim | Overdraw + material levers (#1, #2, #4) |
| GPU spike disappears with `showflag.Particles 0`, Insights shows sim stages hot | GPU simulation cost | Reduce spawn rate / particle count (#3) |
| `Game` high in stat unit + many small emitters | Per-emitter GT tick overhead | Consolidate emitters (#5), lightweight emitters (#8) |
| `Game` high + `System Simulation Tick [GT]` ≥ 1 ms | Heavy params / dynamic bounds / events | Fixed bounds (#6), attribute readers over events, consolidate |
| `Game` high + CPU emitter with 1,000+ particles | Wrong sim target | Move to GPU sim (#3 / sim-target rule) |
| `Draw` high in stat unit + multiple mesh renderers | Too many draw calls | Mesh→sprite or atlas meshes (#9) |
| Hitches when effect spawns (spike, then settles) | Allocation on spawn | System pooling (#7) |
| Cost fine up close but never drops at distance | No culling/LOD | Effect Type distance culling + scalability (§5) |
| Particle lights present + light cost scales with count | Per-particle lights O(n×m) | Remove Light Renderer, emissive material or MegaLights (#10) |
| Refraction/distortion material on effect | Scene-color copy (~2–3× cost) | Non-refractive normal distortion (#10) |
| Particles vanish early / bounds box undersized (Debugger Show Bounds) | Bounds misconfig (correctness, also perf) | Fixed Bounds sized correctly (#6) |
| Cost never goes away after effect "ends" | Infinite loop behavior | Loop Behavior = Once for one-shot effects |

---

### 4. Fix playbook — apply in order, re-measure (§1 steps 1–2) after each

1. **Reduce overdraw with cutout/alpha-erosion textures** — replace rectangular sprite fill with shaped masks; masked instead of additive where binary-opaque. *Gain: 2–4× overdraw reduction; highest ROI.*
2. **Simplify particle material** — remove procedural noise/math, replace with pre-baked texture lookups; combine texture fetches into one sheet. Target < 30 instructions for big sprites. *Gain: 50–60% per-pixel shader cost.*
3. **Cut spawn rate / particle count** — scale spawn 0.5–0.7×; halve GPU max count and re-profile. *Gain: 35–50% (≈30–40% GPU if geometry-bound).*
4. **Shrink sprite scale 0.75–0.9×** — pixel cost scales with area, so small scale cuts pay off quadratically. *Gain: 20–30% render cost on dense effects.*
5. **Consolidate emitters** — merge 3–5 small emitters into one with multiple renderers/spawn groups. *Gain: 60–70% of per-system GT tick overhead.*
6. **Fixed Bounds instead of Dynamic** — System Editor > Fixed Bounds (verify size with Debugger Show Bounds). *Gain: ~0.05–0.1 ms GT per system.*
7. **Pool spawn-heavy systems** — Effect Type > Performance: Pool Prime Size 10–20, Max Pool Size 50+; `ManualRelease` or `FreeInPool`. *Gain: 30–40% spawn cost, kills hitches.*
8. **Lightweight/stateless emitters (UE5.4+)** — preset behaviors only (gravity/drag/noise/scale), no custom modules. *Gain: 40–70% where applicable; removes tick cost entirely.*
9. **Atlas meshes / convert mesh→sprite (esp. mobile)** — sprites = 1 draw call per emitter. *Gain: 0.5–1 ms render thread; 5–15 draw calls.*
10. **Kill expensive renderer features** — remove Particle Light renderers (use emissive or MegaLights, ~50% of per-particle light cost), replace refraction with tangent-space normal distortion (~20% vs ~300% overhead), disable Cast Volumetric Shadow (3–5× light cost), uncheck Require Current Frame Data, disable Interpolated Spawning if not visually needed, set `Max Delta Time = 0.033` to cap spike frames.

If TSR is **disabled** project-wide: try `r.SeparateTranslucency 0` (saves 5–10% of translucency GPU time; revert if artifacts).

---

### 5. Scalability / LOD setup (do this for every shipped effect)

1. **Create/assign an Effect Type asset** (one per effect category: ambient, impact, hero) and assign it in System Properties.
2. **Distance culling:** Effect Type > Culling Distance Start = 2,000–5,000 UU by effect scale; set Max Camera Distance in Emitter Settings. Verify in Niagara Debugger: active system count should drop 30–50% in typical scenes.
3. **Quality-level spawn scaling:** Spawn Count Scale per quality — Low = 0.4×, Medium = 0.7×, High = 1.0×, Epic = 1.2×. Disable expensive emitters entirely at Low. Cull distance per quality: Low = 2,000 / Medium = 3,500 / High = 5,000.
4. **Global budget curves:** with `fx.budget.enabled 1`, set Effect Type Max Instance Count Scale curve (0.0→1.0, 1.0→0.2) and Max Distance Scale curve (0.0→5000, 1.0→1500) so the engine auto-degrades under budget pressure (~20% automatic runtime reduction). Inspect the Niagara Debugger's Global Budget panel (GameThread / RenderThread / GPU usage) to see what gets culled.
5. **Re-validate at each quality level** — repeat §1 steps 1–2 at Low/Medium/High; mobile target ≈ 50–60% of PC spawn rates.

**Sign-off rule:** an effect passes when all §2 golden numbers hold at gameplay camera distance, at High quality, with the worst-case simultaneous instance count (e.g., 5 impacts at once), and an Effect Type with distance culling is assigned.
