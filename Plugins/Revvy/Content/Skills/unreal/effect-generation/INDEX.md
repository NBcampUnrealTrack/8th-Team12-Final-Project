# 레비 이펙트 생성 지식 베이스 — INDEX (먼저 읽기)

VFX(Niagara·머티리얼·텍스처) 작업 시 이 순서로 참고. 모든 수치·레시피는 아래 문서에 있으니 추측하지 말고 해당 문서를 Read.

> **신규 이펙트 요청 = `AESTHETIC_GRAMMAR.md`로 의도와 미학 IR을 만들고, `ARCHETYPE_PROFILES.md`로 gameplay read를 정한 뒤 `PRODUCTION_PIPELINE.md`의 완료 gate를 적용한다.** 프리셋 또는 spec은 시작점이며 compile-only 결과는 완료가 아니다.

> **작업 절차는 tutorial-free 폐루프를 따른다**: `intent -> IR -> 후보 -> 결정론적 capture -> metrics/critique -> causal repair -> 승인`. FASTBUILD는 호출 수를 줄이는 초안 조립법이며, tutorial study와 generic material은 prototype 전용이다.

## 읽는 순서
0. **AESTHETIC_GRAMMAR.md** — 튜토리얼 없이 Primary/Secondary, value, shape, motion, negative space를 설계하는 미학 IR.
0.1. **ARCHETYPE_PROFILES.md** — Impact/Portal/Tornado/Splash/Trail/Aura 등 유형별 gameplay read와 전용 gate. recipe가 아니라 공간·시간·운동 계약.
0.2. **PRODUCTION_PIPELINE.md** — UE 5.8 제작 IR, simulation/renderer/material/Effect Type/bounds 선택, GPT Image·GeometryScript gate, Sim Cache·Debugger 검증, 완료 증거.
0.3. **VISUAL_VALIDATION.md** — structural/runtime/pixel/aesthetic/archetype gate, `/Game/VFX/Validation/Maps/L_RevvyVFXValidation`, SceneCapture2D blank/effect pair, `revvy_vfx_visual_quality.py` CLI와 승인 baseline.
0.4. **CRITIQUE_AND_REPAIR.md** + **FAILURE_ATLAS.md** — 관찰을 원인에 연결하고 한 축씩 수정하는 폐루프와 실패 사례.
0.5. **RECIPE_LIBRARY.md** — ⭐효과별 실측 빌더블 레시피 **18종 + 부록 4종**(이미터표+머티리얼+핵심원칙/#1실수). 텔레포트/실드/체인라이트닝/지면균열/빙결/독구름/홀리/토네이도/회복오라/폭포/화염방사/마법진/바람/앰비언트/슬래시/비눈/다크오라/UI픽업. 부록: 모듈 레퍼런스(80+)·렌더러 속성·셋업패턴(메시/이벤트/GPU)·연동퍼포먼스(Epic 공식 검증). **이펙트 만들기 전 해당 효과 레시피 먼저.**
0.7. **NIAGARA_MAKING.md** — §26 고급 시스템 검증 부록만 보존(Sim Stages/Grid·Stateless·NDC·Data Interfaces·Scratch Pad·Neighbor Grid). ⚠️§1~25는 2026-06-14 사고로 소실(RECIPE_LIBRARY/MATERIAL_FUNCTIONS/NIAGARA_5_7에 핵심 중복 보존).
1. **NIAGARA_5_7.md** — ⚠️ 프로그램 작성 시 필수: 5.7 정확 모듈/핀/enum 스펙(Emitter State 등), enum은 표시명 사용(서비스가 내부명 변환, 숫자 금지), Lifetime은 Initialize Particle의 float(Emitter State enum 아님), LEGACY→현대 대체 표, 워닝→수정 표.
2. **KNOWLEDGE.md** — 최상단 "품질 우선순위"(연출 1순위, 플립북은 틈새) + Golden Numbers(예산/수명/스폰/emissive) + Niagara 빌드 패턴 + 프로덕션 아키텍처. **모든 작업의 기준선.**
2. **미학 IR을 정한 뒤 두 렌더 축을 결정**:
   - **STYLES.md** — 렌더 아트스타일 축(실사 / 툰·셀 / 핸드페인티드 / 애니·가챠 / 픽셀). "어떻게 그려지나" = 셰이딩·셰입·텍스처.
   - **STYLE_GUIDE.md** — 무드 톤 참고값(Horror / Action). emissive·채도·bloom·타이밍 수치는 exposure, camera, archetype 검증 결과에 맞게 조정한다.
   - 두 축은 **직교**이지만 전역 절대 수치가 아니다. 충돌 시 `AESTHETIC_GRAMMAR`의 gameplay read와 검증 capture가 우선한다.
3. **RECIPES.md** (범용) + **OBLIVIO_EFFECTS.md** (이 프로젝트 전용: 음파 공격·다크 오라·핏빛 할퀴기 — 호러 톤 수치/FEEL 페어링 포함, 우선 참조)
   **RECIPES.md** — 효과별(불/연기/스파크/매직/충격파/트레일) 빌드 가능 레시피(에미터 스택+머티리얼 그래프+age 커브+빌드 스텝). **실제 구성의 기준.**
4. **MATERIAL_FUNCTIONS.md** — 재사용 머티리얼 함수(SoftParticle/Dissolve/Panner2Layer/PolarUV/FresnelRim/GradientMap) + **검증 노드체인 부록 2종**(기본6+고급6=12종, Epic 공식+adversarial: dissolve/fresnel[기본 Exp 5.0]/panner/polar[VectorToRadialValue=함수]/soft-particle[DepthFade=Translucent]/subuv[ParticleSubUV UVs 무시] + refraction[UE5.2+ Refraction Mode]/WPO/gradient-LUT/custom-HLSL/parallax/교차발광). 효과 만들기 전 한 번 만들어 재사용.
5. **SEQUENCING.md** — 다단계 연출(Anticipation→Burst→Linger), Timeline+MPC+카메라셰이크, use* 토글, 이벤트 서브이미터.
6. **FEEL.md** — 게임 필 레이어: impact 이펙트에 SFX+카메라셰이크+hit-stop을 같은 프레임에. 강도별 프리셋(light/heavy/explosion/ambient).
7. **PERF.md** — 빌드 후 성능 검증: 측정 명령(stat/Insights/Niagara Debugger) → 진단표 → 수정 플레이북(예산: 시스템당 CPU<1ms, GPU<5ms).
8. **CODEX_PROMPTS.md** — 텍스처가 노드로 안 될 때만 codex 이미지 생성 프롬프트(검증 매트릭스 + 스타일 modifier). 타일링 노이즈는 codex 말고 절차적.

## 공용 텍스처 라이브러리
`TextureLibrary/` 에 codex 사전 생성 텍스처 6종(글로우/스파크/연기/불꽃/룬서클/번개) + MANIFEST.md. **새 텍스처 생성 전에 여기 먼저 확인 → 있으면 임포트 재사용.**

## 핵심 원칙 (절대 규칙)
- 모션은 **머티리얼 연출**(panner/polar/distort/dissolve/dynamic param)로. 플립북은 폭발연기 등 진짜 예외만.
- 단순 도형·노이즈·그라데이션은 **머티리얼 노드**로 직접. 노드로 못 만드는 PNG만 **revvy_gen_texture 헬퍼(codex)** 로.
- 빌드 전 스타일+톤 결정 → RECIPES 골격에 적용 → 검증.
- ⚠️ **에디터 크래시 방지**: 프로그램으로 구성/수정한 NiagaraSystem·머티리얼은 **에디터 창으로 열기(open_asset) 전에 반드시** compile_system/compile_material + diagnostics 검증. 불완전 상태(이미터/렌더러 인덱스 불일치)로 열면 엔진 어설션으로 에디터 전체가 죽음(실사례: NS 열다 Array index out of bounds 크래시). 검증 실패 시 열지 말고 먼저 고치고, 열기는 가급적 사용자에게 맡길 것.

## 문서 역할 요약
| 문서 | 역할 |
|---|---|
| **AESTHETIC_GRAMMAR.md** | **튜토리얼 없는 생성 문법과 기계 판독 가능한 미학 IR** |
| **ARCHETYPE_PROFILES.md** | **유형별 gameplay read, motion law, 전용 gate** |
| **PRODUCTION_PIPELINE.md** | **UE 5.8 프로덕션 IR, 제작 선택 기준, 시각·성능 완료 gate** |
| **VISUAL_VALIDATION.md** | **5단계 시각 gate, SceneCapture2D render-target pair, host-side pixel 분석과 승인 regression baseline** |
| **CRITIQUE_AND_REPAIR.md** | **후보 비교, metric/critique, 원인별 단일 축 수정, 승인 학습 루프** |
| **FAILURE_ATLAS.md** | **동심원, glow ball, clutter, hard pop, 잘못된 증거 등 실패 진단** |
| **PLAYBOOK.md** | **실행 진입점 — 프리셋/spec/머티리얼 선택/레이어링/톤 압축** |
| **RECIPE_LIBRARY.md** | **효과별 빌더블 레시피 18종+부록4(Epic 공식 검증값)** |
| NIAGARA_MAKING.md | §26 고급시스템 검증(나머지 §1~25 소실) |
| NIAGARA_5_7.md | 5.7 정확 모듈/핀/enum 스펙(워닝 방지) |
| FASTBUILD.md | 빠른 초안 조립(조립+값); 최종 검증은 PRODUCTION_PIPELINE 적용 |
| KNOWLEDGE.md | 기준선: 우선순위·수치·아키텍처 |
| STYLES.md | 렌더 스타일 축 |
| STYLE_GUIDE.md | 무드 톤 축(수치) |
| RECIPES.md | 효과별 빌드 레시피(범용) |
| OBLIVIO_EFFECTS.md | 오블리비오 전용 호러 이펙트(음파/다크오라/블러드슬래시) |
| CGHOW.md | CGHow 시그니처 기법(dissolve/슬래시/포탈/스타일라이즈드) |
| CGHOW_TUTORIAL_STUDIES.md | 공개 출처·멤버십 경계·빌드 가능한 prototype 학습 프리셋 5종. 최종 미학 기준이 아님 |
| TEXMAT.md | 텍스처 채널패킹 규격·머티리얼 노드체인·마스터 아키텍처 |
| XOCIETY.md | 프로덕션 구조 레퍼런스(MM→MI 위계·거버넌스) |
| REVVY_PACKS.md | 팩 학습: 8-14 이미터 레이어링·머티리얼 패밀리 레시피 |
| REALISTIC_BLOOD.md | 블러드 4레이어 표준(burst/포물선/데칼/드립)·웻룩 머티리얼 |
| OUTLINE.md | 아웃라인 기법(툰/셀 스타일 축 연계) |
| MATERIAL_FUNCTIONS.md | 재사용 MF |
| SEQUENCING.md | 다단계 연출 |
| FEEL.md | 게임 필(SFX·셰이크·hit-stop) |
| PERF.md | 성능 측정·진단·수정 |
| CODEX_PROMPTS.md | codex 텍스처 프롬프트(보조) |
| LOG.md | 학습 루프 기록(내부) |
