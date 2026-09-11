## Game Feel Layer — SFX + Camera Shake + Hit-Stop

The "punch" of a hit isn't the VFX alone — it's **four channels firing on the same frame**: sound, camera shake, hit-stop (time freeze), and a screen flash, all wrapped around the Niagara impact effect. Miss the sync by even a few frames and the brain reads them as separate events; land them together and a flat collision becomes a *hit*.

This doc is grounded in the Oblivio project's existing assets: `Content/Blueprints/CS_HitShake.uasset` (camera shake), `Content/Sound/Attenuation/ATT_Monster.uasset` / `ATT_InteractiveProps.uasset` (attenuation), `CON_Monster_Voice.uasset` (concurrency), `Mix_Master.uasset` (sound mix), and the `*_Cue` randomized sound cues already in `Content/Sound/`.

---

### 1. The Impact Frame Stack — what fires on frame 0

When a hit lands (projectile `OnHit`, melee `AnimNotify`, enemy death), **everything below executes in the same tick**, wired left-to-right in one Blueprint event so execution order is deterministic:

```
[Impact Event] (frame 0)
   ├─ 1. SetGlobalTimeDilation(0.05–0.15)      ← hit-stop, FIRST
   ├─ 2. PlaySoundAtLocation(impact cue)        ← uses real-time clock, unaffected by dilation
   ├─ 3. ClientStartCameraShake(CS_..., scale)  ← shake
   ├─ 4. SpawnSystemAtLocation(Niagara impact)  ← the VFX itself
   ├─ 5. (optional) screen flash MPC param = 1  ← white overlay, fade over 1–2 frames
   ├─ 6. (optional) LaunchCharacter / AddImpulse ← knockback
   └─ 7. SetTimer( reset dilation→1.0, after hitstop_ms )
```

**Why this order:** hit-stop goes first so the freeze is already active when the VFX/sound register — the player perceives the freeze *as* the moment of contact. Sound uses the audio clock (real time), so it plays at full speed even while game time is frozen — that's correct and desirable (the *crack* shouldn't slow down). The dilation reset is the only deferred step, scheduled by a timer using **unscaled** real time so it doesn't get stuck during the freeze.

**Sync rule:** never spread these across multiple ticks, `Delay` nodes, or separate Blueprints. One event, sequential wires, frame 0. For animation-driven hits, put the whole stack inside a single **AnimNotify (Branching Point** tick type for frame-perfect firing) on the impact frame.

---

### 2. Camera Shake setup (CS_HitShake / MatineeCameraShake)

The project's `CS_HitShake` should be a `CameraShakeBase` (legacy `MatineeCameraShake` / `PerlinNoiseCameraShake` pattern). Create one variant per tier (`CS_LightHit`, `CS_HeavyHit`, `CS_Explosion`) or reuse one and pass a **scale multiplier** to `ClientStartCameraShake`.

Per-shake config (in the Camera Shake BP details):
- **Oscillation Duration**: total shake time (see preset table).
- **Location Oscillation** (X/Y/Z): amplitude in units, frequency in Hz — independent per axis.
- **Rotation Oscillation** (Pitch/Yaw/Roll): amplitude in degrees (small: 0.02–0.15), frequency in Hz.
- **FOV Oscillation**: optional extra punch on big hits.
- **Blend In / Blend Out**: 0.05–0.15s easing so the shake fades instead of hard-snapping.
- **Pattern**: `Perlin` for chaotic high-intensity (explosions/heavy), `Wave` (sinusoidal) for small rhythmic hits.

Triggering:
- **Player-local:** `Get Player Controller → ClientStartCameraShake(ShakeClass, Scale)`. Scale 1.0 = baseline, 1.5–2.0 for emphasis.
- **World-space / distance-attenuated:** `PlayWorldCameraShake(Shake, Epicenter, InnerRadius, OuterRadius, Falloff)` — full intensity inside Inner (500–1000), zero past Outer (3000–5000). Use this for enemy deaths / environmental impacts away from the player.
- **Controller rumble pairing:** after the shake, `PlayDynamicForceFeedback` matched to the shake duration (80–200ms) and intensity (0.3 weak → 1.0 strong).

---

### 3. Sound playback setup

- **Trigger:** `PlaySoundAtLocation(Location = impact point, Sound = <Cue>, VolumeMultiplier = 1.0, PitchMultiplier = 1.0)` on the impact frame. For UI/non-spatial hits use `PlaySound2D`.
- **Use the `_Cue` assets** already in the project — Sound Cues with a **Modulator node** give free per-shot variation so repeated hits don't sound mechanical:
  - Pitch randomization: **−0.5 to +0.5 semitones**
  - Volume randomization: **−12 dB to +6 dB**
- **Attenuation:** assign the existing `ATT_Monster` / `ATT_InteractiveProps` (or a new `ATT_Impacts`):
  - Inner Radius (full volume): **300–500 units**
  - Falloff Distance: **1400–3000 units**, Logarithmic falloff
  - Spatialization: ON, **HRTF** algorithm for 3D depth/panning
  - Air Absorption: Min **300** / Max **1500** units
- **Concurrency** (use/extend `CON_Monster_Voice`): in impact-heavy scenes cap voices to **4–8**, Resolution Rule **STOP_FARTHEST_THEN_OLDEST**, Voice Steal Release **0.15s** — prevents 20 simultaneous hits from clipping into mud.
- **Layered impact (MetaSound)** — structure one impact sound as 3 parallel chains so it reads as a physical hit:
  - **Transient** (attack, click 8–16kHz, ~10–50ms) — gain ~0.4
  - **Body** (mid tone 200–400Hz, ~20–50ms) — gain ~0.5
  - **Tail** (reverb/ambience, 100–500ms) — gain ~0.1
- **Niagara-driven audio** (for effects that own their sound, e.g. fire — see `SA_FireBig/Medium/Small`): add a **Play Audio** module (Emitter Spawn group → fires frame 0) for one-shots, or **Play Persistent Audio** when you need to fade/parameterize while the effect lives.

---

### 4. Hit-stop / time dilation

- **Global freeze:** `SetGlobalTimeDilation(0.05–0.15)` then `SetTimer` to reset to `1.0` after the hit-stop window. The timer must use **real (unscaled) time** or it'll fire late during the freeze.
- **Local freeze (preferred for 1-on-1):** set `CustomTimeDilation = 0` on *both* attacker and defender only — keeps the rest of the world (other enemies, ambient VFX) moving. Avoid `SetGlobalTimeDilation` when only the duel should freeze.
- **Duration by tier:** 50ms (light) → 200ms (explosion). Reference points: fighting-game hit-stop runs ~8/12/15 frames (light/medium/heavy); beat-em-ups ~6–10 frames. At 60fps, 1 frame = 16.67ms — so **always specify hit-stop in ms**, not frames, for frame-rate independence.
- **Screen flash:** drive a Material Parameter Collection scalar (white post-process overlay) to 1.0 on frame 0, fade to 0 over 1–2 frames (16–33ms). Optional chromatic aberration: ramp during the freeze, return to 0 on recovery.

---

### 5. Per-tier preset table

| Tier | Hit-Stop | Camera Shake (Loc amp / Rot amp / Freq / Dur / Blend) | Pattern | Sound | Flash | Rumble |
|------|----------|--------------------------------------------------------|---------|-------|-------|--------|
| **Light hit** | 50–80ms (dilation 0.15) | Loc 0.3–0.5u / Rot 0.02–0.05° / 10Hz / 0.15s / 0.05s | Wave | 1 cue, pitch ±0.5, vol ±6dB; attn 300/1400 | none (or 1 frame) | 0.3 amp, 80ms |
| **Heavy hit** | 100–150ms (dilation 0.1) | Loc 0.8–1.2u / Rot 0.05–0.08° / 12Hz / 0.25s / 0.08s | Wave/Perlin | layered cue (transient+body), pitch ±0.3; attn 400/2000 | white 1–2 frames | 0.7 amp, 150ms |
| **Explosion** | 150–200ms (dilation 0.05) | Loc 1.5–2.0u / Rot 0.1–0.15° / 15Hz / 0.4s / 0.1s (+FOV) | Perlin | full 3-layer MetaSound + tail; attn 500/3000; concurrency 4–8 | white 2 frames + chromatic | 1.0 amp, 200ms |
| **Far explosion** | none (too distant) | Loc 0.5–1.0u / Rot 0.05–0.1° / 8Hz / 0.5s / 0.15s, via `PlayWorldCameraShake` Inner 800 / Outer 4000 | Perlin | attn-driven, low volume by distance | none | none/light |
| **Ambient** (fire, drips, idle FX) | **none** | **NONE** — no shake | n/a | Persistent Audio loop in Niagara, full HRTF attenuation, looping concurrency | none | none |

**Tier-selection guideline:** match the tier to the *cause*, not the VFX size. A small spark from a heavy slam is still a Heavy hit. Ambient/persistent effects (`SA_FireBig`, drips, environmental loops) get **sound + attenuation only — never shake, never hit-stop**, or the screen rattles constantly and the impact tiers lose all meaning.
