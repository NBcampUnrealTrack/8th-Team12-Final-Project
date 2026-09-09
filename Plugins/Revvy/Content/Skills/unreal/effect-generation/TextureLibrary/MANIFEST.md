# 공용 VFX 텍스처 라이브러리 (codex 사전 생성)

레시피들이 공통으로 쓰는 codex-적합 텍스처. **이펙트 빌드 시 새로 생성하기 전에 여기 있는지 먼저 확인** —
있으면 `/Game/VFX/Textures/`로 임포트해 재사용 (AssetImportTask 또는 revvy_gen_texture의 임포트 함수).

| 파일 | 용도 | 임포트 설정 | 쓰는 레시피 |
|---|---|---|---|
| T_SoftGlow.png | 범용 파티클 글로우 닷 (additive) | sRGB ON, Default 압축 | 스파크/매직/홀리/먼지 등 거의 전부 |
| T_SparkStreak.png | velocity-stretched 스파크 줄기 | sRGB ON | 스파크/임팩트/엠버 |
| T_SmokePuff.png | 단일 연기 퍼프 (grayscale 밀도) | **sRGB OFF**, Sampler=LinearGray | 연기/먼지/독구름 |
| T_FireWisp.png | 단일 불꽃 릭 | sRGB ON | 불 (스크롤 노이즈와 곱) |
| T_MagicRunic.png | 룬 매직 서클 (시안, 원형 대칭) | sRGB ON | 매직/홀리 (PolarUV 회전과 조합) |
| T_LightningArc.png | 수직 번개 아크+분기 | sRGB ON | 전기/번개 리본 |

공통: 검은 배경 = additive에서 투명. UE 임포트 후 Mip Gen 기본, 파티클용은 Never Stream 권장.
타일링 노이즈/그라데이션/radial은 여기 없음 — 머티리얼 노드(절차적)로 만들 것 (CODEX_PROMPTS 라우팅 규칙).

## 팩 추출 텍스처 (플러그인 내장 — /Revvy/VFX/Textures/, .uasset 즉시 사용 가능)

Revvy 팩(Niagara_Effects03·StylizedFX_3)에서 추출한 검증된 범용 15종. **임포트 불필요 — 경로 그대로 머티리얼에 바인딩.**

| 에셋 | 용도 | 비고 |
|---|---|---|
| T_Noise_Vector | 만능 벡터 노이즈(디스토션·erosion) | 원팩 40회 참조 — 최우선 |
| T_Aura_GlowBase | 매직/글로우 베이스 | 원팩 38회 참조 |
| T_Normal_Water_Distortion | 굴절/열기 노멀 | 18회 참조 |
| T_Noise_Procedural_Large / T_Noise_Perlin_RGB | 대형/RGB 채널 노이즈 | RGB는 채널별 다른 노이즈 |
| T_Mask_Alpha | 범용 알파 마스크 | |
| T_Shockwave_Radial | 쇼크웨이브 링 | 음파/충격파 |
| T_ColorPalette_LUT / T_Gradient_Ramp_V | 색 LUT·그라데이션 램프 | GradientMap용 |
| T_Flow_Distortion | 플로우맵 | TEXMAT [-1,1] 공식 적용 |
| T_Trail_Generic / T_Glow_Additive / T_Flash_Shape | 트레일/글로우/플래시 셰입 | additive |
| T_Smoke_Flipbook_Large / T_Smoke_Flipbook_8x4 | 연기 플립북(SubUV) | 8x4는 스타일라이즈드 |

## 블러드 텍스처 (RealisticBlood 추출 — /Revvy/VFX/Textures/, 즉시 사용)

| 에셋 | 용도 |
|---|---|
| T_BloodSplatter_Core_01 (+_N_01 노멀) | 핵심 스플래터 + 웻룩 노멀 페어 |
| T_BloodSpray_Directional_01 / T_BloodSpray_Heavy_01 | 방향성 스프레이(임팩트 각도 매칭) |
| T_BloodGore_Thick_01 / T_BloodSplatter_Smooth_01 | 고어 진득/부드러운 변형 |
| T_BloodSplotch_Organic_01 / T_BloodDecal_Splat_01 | 데칼 얼룩 |
| T_BloodPuddle_01 | 바닥 고임 |
| T_BloodDroplets_01 | 잔방울 스팟 |
| T_Normal_LiquidWave / T_LiquidReflection_Gloss | 액체 웻룩 노멀·리플렉션 (피 외 액체에도) |

블러드 빌드는 REALISTIC_BLOOD.md 4레이어 표준(burst/포물선/데칼/드립) 참조. 원본 풀 변형(60+ 스플래터)은 Content/RealisticBlood/_Commons.

