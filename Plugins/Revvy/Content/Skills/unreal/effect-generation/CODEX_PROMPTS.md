# 레비 Codex VFX Texture Prompt Library (image-gen, English)

Copy-paste templates. Replace `<subject>` and bracketed slots. Constraints are baked per type.

**Universal rules baked in:**
- Additive types → `pure black RGB(0,0,0) background` (black = transparent).
- All → `single centered element, ~70% frame height, no baked shadows, no ambient occlusion, no soft glow halo, pure diffuse, sharp edges, 8-bit sRGB`.
- Masks/noise/flow → `grayscale, linear (no gamma baking), seamless tileable`.
- Flipbooks → `exact NxN grid, uniform cell spacing, seamless frame edges, power-of-two`.
- Resolution: 512 or 1024 base; flipbook atlas = grid × tile (4x4@512 → 128px tile).

## ✅ Validated (Iteration 2 — real codex gen tested)
| 타입 | codex 적합도 | 비고 |
|---|---|---|
| 불/링/스파크/임팩트/매직/트레일 (additive 단일 스프라이트) | **강함 ✅** | 검은배경·중앙·핫코어 정확. 템플릿 그대로 사용 |
| 동심 링/충격파 | **강함 ✅** | 대칭·additive 밝기 양호 |
| **플립북 (4x4 SubUV 시트)** | **강함 ✅** | 16셀 전부 수평 중앙 정렬(cx 0.46~0.51), 점화→피크→소멸 진행 정확. 검증됨 — 단 ≤6x6 권장(8x8은 셀이 작아 정렬 흔들릴 위험), 머티리얼 SubUV frame blending으로 프레임 점프 보정 |
| **seamless 타일링 노이즈/마스크** | **약함 ❌** | codex "seamless"는 실제로 안 이어짐 — 2x2 반복 시 시임 발생 확인됨 |

**라우팅 규칙 (중요):**
- **seamless 타일링이 필요한 노이즈/에로전/플로우맵 → codex 쓰지 말고 절차적으로 만들 것.** UE 머티리얼 `Noise` 노드(또는 numpy 주기적 Perlin: `np.sin(2π·x)` 기반 wrap)로 생성하면 진짜 타일링됨.
- codex 노이즈는 **타일링 불필요한 1회성 유기적 디테일**(예: 단일 연기 퍼프, 균열 데칼)에만.
- 꼭 codex 노이즈를 타일링해야 하면: 생성 후 offset(50%,50%) + 경계 16~32px linear 블렌드 패스로 수동 보정.

---

## FIRE — single frame (additive, sRGB ON)
```
High-quality game particle texture of <subject: fire flame burst>, additive blend, centered single element ~70% frame height, pure black RGB(0,0,0) background, soft orange-yellow hot core fading to darker edges, no shadows, no ambient occlusion, no baked bloom, sharp fine detail, flat even lighting, 512x512, 8-bit sRGB, suitable for Unreal Niagara additive VFX
```

## FIRE — flipbook atlas (additive)
```
Fire VFX flipbook sprite sheet of <subject: evolving flame burst>, exact <NxN: 8x8> grid (<frames: 64> sequential frames), uniform cell spacing, each frame a centered flame on pure black RGB(0,0,0) background, orange-red core with yellow highlights, vertical upward flow progression small core to expanding flame to dissipate, seamless frame edges, no shadows, high contrast, <res: 1024x1024>, 8-bit sRGB, Unreal Niagara SubUV ready
```

## SMOKE — flipbook (translucent, grayscale)
```
Smoke VFX flipbook sprite sheet of <subject: billowing smoke plume>, exact <NxN: 8x8> grid (<frames: 64> frames), uniform spacing, grayscale density map light-gray to white on pure black background, organic Perlin/Simplex cloud shapes, soft feathered edges, vertical billowing progression dense center to wispy dissipation, seamless edges, no shadows, no hard edges, <res: 2048x2048>, suitable for translucent soft-particle smoke in Unreal Niagara
```

## SPARKS / EMBERS — single sprite (additive)
```
Game VFX particle sprite of <subject: electric spark / glowing ember>, bright yellow-white radial center with radiating jagged tendrils fading outward, pure black RGB(0,0,0) background, soft glow halo around core, centered single element, no shadows, high contrast, additive-blend ready, 512x512, 8-bit sRGB, grayscale-friendly, seamless tileable, Unreal Niagara magic/energy particle
```

## NOISE / EROSION / DISSOLVE MASK — single channel (linear)
```
Grayscale <subject: Perlin / Voronoi erosion> noise mask, <octaves: 3-4> octaves, full 0-255 range black to white, linear no gamma baking, single channel, high contrast for alpha clipping, seamless tileable on all four edges (no visible seam at 2x2 or 4x4 repetition), fine detail at <res: 512x512>, sRGB OFF, suitable for Unreal Niagara dissolve / erosion / lifetime mask
```

## GRADIENT MASK — erosion/feather (linear)
```
Grayscale gradient mask, <subject: radial center-to-edge / linear top-to-bottom>, smooth white-to-black, 2% noise overlay to break banding, full 0-255 linear range no gamma, single channel, seamless edges, <res: 256x256>, sRGB OFF, suitable for Unreal alpha erosion and soft-particle feathering
```

## ENERGY RING / SHOCKWAVE (additive)
```
Game VFX texture of <subject: concentric energy ring / expanding shockwave>, bright <color: cyan-blue> rings radiating from exact center, pure black RGB(0,0,0) background, sharp inner edge soft outer falloff, perfect circular symmetry, centered, additive-compatible bright values, no shadows, 512x512, 8-bit sRGB, Unreal Niagara impact / ring VFX
```

## ENERGY RING — flipbook (expanding)
```
Shockwave flipbook sprite sheet of <subject: expanding energy ring>, exact <NxN: 4x4> grid (<frames: 16> frames), uniform spacing, white-blue concentric rings expanding from tight center outward across frames, pure black background, sharp definition, seamless frame edges, additive ready, <res: 512x512>, 8-bit sRGB, Unreal Niagara SubUV impact ring
```

## WATER — splash / caustic (additive or translucent)
```
Game VFX texture of <subject: water splash droplets / caustic interference>, blue-white highlights on pure black background, radiating high-contrast pattern, sharp crisp detail, centered single element, no shadows, seamless tileable edges, <res: 512x512>, 8-bit sRGB, suitable for Unreal Niagara impact / underwater VFX (caustics: use NoMipmaps, additive)
```

## IMPACT / MUZZLE FLASH (additive)
```
Game VFX texture of <subject: muzzle flash / impact burst>, bright orange-white radial spikes emanating from a centered point source, pure black RGB(0,0,0) background, high contrast sharp detail, no shadows, no soft halo, isolated single flash, 512x512, 8-bit sRGB, Unreal Niagara additive impact
```

## MAGIC AURA / GLOW (additive)
```
Magical aura glow texture of <subject: radial energy halo / sparkle burst>, bright <color: cyan-purple/gold> glowing center fading smoothly to transparent edges on pure black background, concentric soft radial glow, centered, no shadows, additive-blend optimized, flat lighting, 512x512, 8-bit sRGB, Unreal Niagara character aura / energy
```

## TRAIL / SLASH (additive)
```
Weapon trail VFX texture of <subject: sword slash energy arc>, bright <color: white-blue> energy core fading to soft edges, long thin curved tapered shape with sharp ends, pure black RGB(0,0,0) background, motion-blur feel, centered, no shadows, 512x512, 8-bit sRGB, suitable for Unreal Niagara ribbon trail (additive)
```

## SEAMLESS NOISE — texture fill (linear)
```
Seamless tileable <subject: Perlin/Voronoi> noise texture, <octaves: 4-6> octaves grayscale continuous full 0-255 range, organic cloud/cellular pattern, no visible seam at 2x2 or 4x4 repetition with rotation, linear non-gamma-baked, fine detail at <res: 2048x2048>, sRGB OFF, single channel, for Unreal Niagara particle trail / debris fill

---
## Channel-pack note
When packing multiple masks into one RGBA, generate each channel separately with the NOISE/GRADIENT templates above, then composite (R/G/B/A) and import sRGB OFF, Sampler=Masks. Put the highest-contrast mask in the Alpha channel.
```

---

## Codex Texture Prompt — Style Modifiers

> ✅ **검증됨 (Iteration 9)**: 동일 효과(매직 에너지 버스트)를 셀/핸드페인트/애니 modifier로 실제 생성한 결과 셋 다 명확히 다른 스타일로 나옴 — 셀=하드 플랫 밴딩+볼드 아웃라인+기하 스타버스트, 핸드페인트=붓터치+따뜻한 채도+유기적 꽃잎, 애니=크리스프 그라데이션+글로시 블룸+룬 동심. 스타일 키워드 블록이 효과적이니 그대로 사용. additive 효과는 'pure black background'를 항상 유지(검은 배경 정확히 나옴).

> APPEND one of these blocks to the end of any base template in **CODEX_PROMPTS.md** so generated textures match the project render style. The base template still sets subject/layout/background/blend; these modifiers override the *look*. Copy-paste ready. (Reminder from CODEX_PROMPTS routing: codex is weak at true seamless tiling — keep noise/erosion/flowmaps procedural regardless of style.)

### Realistic / Semi-Realistic — APPEND
```
, photorealistic physically-based look, volumetric depth, soft anti-aliased depth-faded edges, hierarchical Perlin noise (large rolling shapes + fine high-frequency detail), turbulent organic diffusion, realistic combustion color space (white-yellow hot core 16-bit HDR to orange-red transition, darkening soot edges), continuous gradient falloff, cinematic motion-blur feel, natural noise-based erosion (Voronoi/Perlin edge break-up, not uniform fade), grounded muted palette, NO cel-shading, NO outlines, NO flat colors, NO posterization
```

### Toon / Cel-Shaded — APPEND
```
, cel-shaded toon style, flat bold colors limited to 2-3 color bands (posterized, hard color steps, NO smooth gradient), bold thick black ink outline, crisp hard sharp edges, NO anti-aliasing, comic-book / manga / graphic-novel aesthetic, high contrast, flat unlit shading, vector-clean linework, geometric angular shapes (not organic), point-sampled hard pixel-crisp edges
```

### Stylized Hand-Painted — APPEND
```
, hand-painted painterly style, visible brush strokes and impasto paint texture, warm saturated vibrant colors (oranges, golds, reds, saturated greens — avoid pale/desaturated), chunky puffy exaggerated shapes with soft density gradients, sharp alpha-crushed inner form with soft outer glow, high-contrast internal detail suitable for gradient-map color lookup, crafted handmade non-photorealistic illustrated quality, grayscale base with rich internal gradients (not flat gray)
```

### Anime / Gacha — APPEND
```
, anime energy-effect style, crisp bright gradients (white-to-color HARD edge, NOT soft gaussian blur), high saturation single-hue emissive, glossy digital polish, additive bloom-ready bright values, geometric sharp shapes (stars, diamonds, hexagons, sharp shards — NOT soft clouds), clean crisp linework, lens-flare sparkle accents, element-coded pure color (blue ice / red fire / yellow lightning / cyan wind), kaleidoscopic radial/polar symmetry, ultra-sharp high-texel-density edges, NO blur
```

### Pixel / Retro — APPEND
```
, retro 8-bit / 16-bit pixel-art style, hard blocky pixelated edges, limited reduced color palette (NES 64-color / PICO-8 16-color feel), ordered Bayer dithering for shading and gradients, NO anti-aliasing, NO soft edges, chunky square shapes, thick 3-5px pixel lines on cardinal/45-degree angles only, low-resolution pixel-grid quantized, point/nearest sampling look, sharp pop-in crisp sprite, arcade-cabinet aesthetic
```

### Geometric / Abstract Minimalist — APPEND
```
, flat geometric minimalist style, perfect clean shapes (precise circles, rectangles, triangles, polygons — no organic deformation), bold flat solid colors, sharp clean vector edges, concentric / nested / radial-grid composition, radial-gradient or distance-field falloff, high-contrast flat fills, NO texture detail, NO noise, NO outlines, modern motion-graphics / editorial design aesthetic, mathematically clean symmetry
```
