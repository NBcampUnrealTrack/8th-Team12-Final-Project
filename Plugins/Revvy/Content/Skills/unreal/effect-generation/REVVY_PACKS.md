## Revvy 팩 학습 (Niagara_Effects03 + StylizedFX_3)

### (a) NS 구조 패턴
- **이미터 레이어링 (8~14개/시스템)**: 코어 임팩트(1-2, SpawnBurst+고휘도) → 블룸/글로우(2-3, 저밀도 SpawnRate) → 파편/트레일(2-3) → 라이트(1-2) → 디스토션(1-2). "순간(Burst)"과 "여운(Rate)"을 이미터로 분리하는 것이 핵심 연출 문법.
- **모듈 스택**: 속도 체인 `AddVelocity → ApplyInitialForces → SolveForcesAndVelocity → ClampVelocity`(속도 상한 강제). 포스 합성은 `CurlNoise(유기적 흐름) + AccelerationForce(중력 낙하) + VortexForce(회전 폴리시)` 3층.
- **로케이션 다양화**: CylinderLocation(수직 에너지/슬래시) ↔ SphereLocation(방사형 임팩트)을 한 시스템 안에서 혼용해 단일축 스폰을 피함. BoxLocation은 볼륨 제한용.
- **렌더러 믹스**: Sprite 기본 + Mesh(블레이드/트레일) + Ribbon(에너지 스트림, 검기 계열) + **LightRenderer는 마지막 글로우 이미터 1개에만** 달아 오버헤드 절감.
- **연출 조합 공식**: Flash(중심, ColorFromCurve white→hue) + Distortion(2차 이미터에 굴절 머티리얼) + Sparks/Debris + Glow + Trail. Collision 모듈은 루트/물리 파편 전용 — 에너지/마법 이펙트엔 쓰지 않아 ethereal 느낌 유지.
- **사이즈/컬러**: CalculateSizeByMass + ColorFromCurve로 파라미터 바인딩 일원화(별도 사이즈 모듈 지양).

### (b) 머티리얼 패밀리별 레시피
| 패밀리 | 노드 수 | 핵심 체인 | 블렌드 |
|---|---|---|---|
| 플래시 | 7-8 | TextureSample × ParticleColor → Emissive, **DepthFade → Opacity**(근접 아티팩트 방지) | Additive |
| 빔 | 10-11 | TexCoord → **Panner** → TextureSample → **Power**(falloff) → Multiply → Emissive | Additive |
| 글로우 | 13-16 | Panner 애니메이션 + Power + Engine_MaterialFunctions01 호출 | Additive |
| 매직 | 13-16 | **Fresnel 림** → Power → Multiply → Emissive 가산, ComponentMask 채널 블렌드 | Additive |
| 플라즈마 | 13-16 | AppendVector 레이어링 + **LinearInterpolate 모핑** | Translucent |
| 디스토션 | 7-11 | Normal(T_Water01_N) → Panner → **Clamp(필수, 래핑 방지)** → SphereMask 국소 falloff | Translucent |

- **전 68개 머티리얼 공통**: ParticleColor 종단 바인딩(나이아가라 컬러/알파 제어), DynamicParameter 4채널(Param1=Panner 속도, Param2=Power 강도, Param3=컬러/모핑 블렌드, Param4=2차 왜곡 강도), 고재사용 패밀리는 TextureSampleParameter2D로 MI 텍스처 스왑.
- **텍스처 3층 스택**: 베이스(T_aura01 등) + 흐름(Panner→T_noise_vector01) + LinearInterpolate 블렌드 → Power 출력.
- **금지 사항**: Additive로 충분한데 Translucent 사용 X, 디스토션에 Clamp 생략 X, ParticleColor 생략 X, 텍스처 하드코딩 X.

### (c) 스타일라이즈드 룩 구성 (StylizedFX_3)
- **Unlit + Translucent 기조**, Emissive+Opacity만 출력(metallic/roughness/normal 디테일 없음) — 라이팅 비의존 VFX 합성.
- **3층 텍스처 문법**: Noise(Perlin/Fractal 흐름 왜곡 드라이버) + **Gra_V 그라디언트 램프(컬러 룩업)** + Mask(하드 알파 컷아웃). MF_Textures_Flow / MF_Textures_Flow_Lerp / MF_Cutout 머티리얼 펑션 커널로 전 패밀리 재사용.
- **실루엣 우선주의**: 실린더/링/토네이도 헬릭스/포탈 메시가 형태를 먼저 만들고 텍스처는 디테일 보조 — 순수 스프라이트 접근의 반대, 원거리 가독성 확보.
- **Fresnel 반전 + 고 HDR emissive** = 밝은 코어 + 실루엣 페이드(아니메/가챠 글로우).
- **단일 머티리얼 + 텍스처 스왑 변형**(BlackHole 00/Cyber/Star) = MI 파라미터로 이펙트 변형 라이브러리.
- **스모크 LOD**: Volume(근접) / Billboard SubUV 8x4·8x8 플립북(원거리) / CylinderSmoke. 노멀맵은 액체/용암 계열에만 한정.

### (d) 우리 팔레트/마스터/레시피 채택 (5)
1. **마스터 머티리얼 공통 규약**: ParticleColor 종단 + DynamicParameter 4채널 표준(1=속도/2=강도/3=블렌드/4=왜곡) + TextureSampleParameter2D — 모든 Revvy VFX 마스터에 강제.
2. **MF 커널 3종 구축**: MF_Rev_Flow(노이즈 UV 왜곡+Panner), MF_Rev_Ramp(Gra_V 그라디언트 룩업), MF_Rev_Cutout(스텝 컷아웃) — Noise+Ramp+Mask 3층 스택으로 스타일라이즈드 이펙트 조립.
3. **이미터 레이어링 템플릿**: Burst 코어 + 저밀도 Rate 글로우 + 파편/트레일 + 최종 Light 1개 — 임팩트/여운 분리, Light는 항상 마지막 이미터 단독.
4. **디스토션 표준 모듈**: T_Water01_N → Panner → Clamp(+SphereMask 국소화), 항상 2차 이미터에만 적용 — 7노드 초경량이라 모바일까지 안전.
5. **컬러 연출 표준**: ColorFromCurve white 피크(0프레임)→hue 페이드 + 컬러는 머티리얼/MI에 베이크 — 기존 학습(ScaleColor 프레임 누적 부패)과 일치하는 안전 경로.

## ⚠️ 레거시 격리 (필수)
 는 **Cascade(레거시) 파티클 시스템** — 직접 사용·복사·스폰 금지(레거시 경고 + 차기 버전 제거 대상). 이 팩에서는 **머티리얼·텍스처·연출 아이디어만** 참고. 같은 룩이 필요하면 Niagara로 재구축(팔레트 블록 + 추출 텍스처 사용). Niagara_Effects03의 Particles/는 Niagara(NS)라 참고 OK — 단 4.23 기준이라 모듈 구성만 참고하고 새로 만들 땐 NIAGARA_5_7 카탈로그 경로 사용.

