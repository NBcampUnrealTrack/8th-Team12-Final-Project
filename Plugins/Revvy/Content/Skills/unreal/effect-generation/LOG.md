# Effect-Generation Quality — Self-Improvement Loop Log

레비의 이펙트(Niagara + 머티리얼 + codex 텍스처) 생성 품질을 반복적으로 끌어올리는 루프 기록.
지식은 KNOWLEDGE.md / CODEX_PROMPTS.md 에 누적한다. 시스템 프롬프트는 이 폴더를 Read하라고만 지시하므로,
다음 반복부터는 이 .md 파일들만 수정하면 되고 C++ 재빌드는 필요 없다.

## Iteration 1 — 기반 지식 구축 (완료)
- 5개 도메인 병렬 리서치: Niagara 펀더멘털 / VFX 텍스처 오서링 / 효과별 레시피 / 이미지젠 프롬프트 / VFX 머티리얼.
- 산출물:
  - `KNOWLEDGE.md` — Niagara 모듈 스택·CPU/GPU 기준·파티클 예산·플립북/채널패킹/타일링 스펙·머티리얼 셋업·효과별(불/연기/스파크/매직/임팩트/트레일) 레시피.
  - `CODEX_PROMPTS.md` — 효과 타입별 codex(gpt-image) 텍스처 프롬프트 템플릿(additive=검은 배경, 마스크=그레이스케일, 플립북=NxN 그리드, 노이즈=seamless).
- 시스템 프롬프트: VFX 작업 전 위 두 파일 Read 지시 추가 (1회 재빌드).

## Iteration 2 — 실생성 검증 → 프롬프트 보정 (완료)
- codex로 3종 실제 생성·육안 비평: 불(additive 스프라이트), 노이즈 마스크, 동심 에너지 링.
- 결과:
  - 불/링 = codex **강점 확정** (검은배경·중앙·핫코어·대칭·additive 밝기 정확, 바로 사용 가능).
  - 노이즈 = **2x2 타일에서 시임 발생 확인** → codex "seamless"는 실제 타일링 안 됨.
- 보정: CODEX_PROMPTS.md 상단에 "Validated 매트릭스 + 라우팅 규칙" 추가 — seamless 타일링은 절차적(머티리얼 Noise/numpy 주기 Perlin), codex는 additive 스프라이트·1회성 유기 디테일에 한정.

## Iteration 3 — 레퍼런스 팩(Level_Up_VFX) 구조 학습 (완료)
- 사용자 소유 VFX 팩의 NS/NE/머티리얼/BP .uasset 바이너리를 5에이전트 병렬로 strings 추출·분석.
- 학습한 프로덕션 패턴(KNOWLEDGE.md에 "Production Niagara & Material Architecture" 섹션 추가):
  - 모듈식 시스템 구성: 통합 NS_*_FULL + part 시스템(Base/Ground/Sparkles/Arrows), part는 FULL에서 emitter 빼서 파생.
  - 재사용 emitter NE_(_Emitters/ 폴더) 상속 + _01/_02 변형 + EmitterState_LoopDelay 타이밍 재사용.
  - 마스터 머티리얼(M_VFX_AdvParticle, Translucent) → MI 파생, 색은 MPC_CustomColor(Color_01/02/Fresnel) + MID 런타임 제어.
  - mesh/ribbon = Fresnel material function rim light; SpawnBurst_Instantaneous + RandomRangeInt; ColorFromCurve/ScaleSpriteSize/AddVelocityFromPoint(radial) 모듈; Scalability 항상 포함.
  - BP: use* 토글로 part 스폰, ColorPicker→MPC + Timeline로 런타임 색/연출, _Custom/_Test 페어.
- naming 규칙(NS_/NE_/M_/MF_/MI_/MPC_/T_) 채택.

## Iteration 4 — 플립북 생성 검증 (완료)
- codex로 1024x1024 불꽃 4x4 플립북(16프레임) 실생성 → 프로그램으로 셀별 밝기·중심 분석.
- 결과: 16셀 전부 수평 중앙 정렬(cx 0.46~0.51 일관), 밝기 점화(m2)→피크(m30)→소멸(m0) 진행 정확. **codex 플립북 4x4 사용 가능 확정.**
- 보정: CODEX_PROMPTS 매트릭스에 "플립북 강함 ✅" 추가 — ≤6x6 권장(8x8 정렬 위험), 머티리얼 SubUV frame blending으로 프레임 점프 보정.
- 수정된 인식: 이미지 모델이 NxN 격자 못 만든다는 가정은 codex(4x4)엔 틀림. (타일링 noise는 여전히 약점 — iter2.)

## Iteration 5 — 방향 정정: 연출 중심 재가중 (완료)
- 사용자 지적: 품질의 핵심은 플립북 셀 애니가 아니라 Niagara 오케스트레이션 + 머티리얼 연출(모션). 맞음 — Level_Up 팩도 mesh/ribbon + 스크롤/Fresnel 방식이었음.
- 6에이전트 리서치: UV 모션(Panner/Polar/Flowmap distortion), 생애주기 Dynamic Material Parameter, mesh/ribbon/GPU curl, 스타일라이즈드 셰이딩, 최소 텍스처 라이브러리.
- 반영:
  - KNOWLEDGE.md 최상단에 "품질 우선순위 배너 + Material & Niagara-Driven 연출(PRIMARY)" + "최소 재사용 텍스처 라이브러리(procedural/codex 구분)" 섹션 prepend (22.7KB→30.9KB).
  - 시스템 프롬프트 이펙트 블록을 연출-우선 순서로 재작성: ①Niagara/머티리얼 설계 먼저 ②모션은 머티리얼(Panner/distort/dissolve/dynamic param), 플립북 기본 미사용 ③shockwave/trail=mesh/ribbon+스크롤+Fresnel ④텍스처는 단순 재사용 입력(노드 우선) ⑤노드 불가 PNG만 codex ⑥플립북은 폭발연기 등 예외+≤6x6.
- 인식 교정: codex는 "모션 생성기"가 아니라 "머티리얼이 움직일 단순 텍스처 공급". 플립북 과집중(iter2/4) 폐기.

## Iteration 6 — 효과별 빌드 레시피 + 머티리얼 함수 라이브러리 (완료)
- 6개 효과(불/연기/매직 aura/충격파 ring/스파크/에너지 트레일) 병렬로 완성형 레시피 작성: Niagara 스택·머티리얼 그래프·age 다이내믹 파라미터 커브·텍스처 입력(procedural/codex 구분)·mcp 빌드 스텝.
- 산출물:
  - `RECIPES.md`(16.3KB) — 효과별 빌드 가능 수준 레시피(불=스크롤2층+age dissolve→ember, 연기=panned+flowmap+soft particle, 매직=polar swirl+fresnel, 충격파=mesh ring+RadialGradient+scale curve, 스파크=GPU burst+velocity-aligned, 트레일=ribbon+length gradient).
  - `MATERIAL_FUNCTIONS.md`(8KB) — 재사용 MF 라이브러리(MF_SoftParticle/DepthFade, MF_Dissolve, MF_Panner2Layer, MF_PolarUV, MF_FresnelRim, MF_GradientMap) inputs/outputs+노드그래프.
  - KNOWLEDGE.md에 연계 문서 cross-ref 추가 (재빌드 불필요).

## Iteration 7 — 톤 스타일 가이드 + 다단계 시퀀싱 (완료)
- 5에이전트 리서치: 호러 톤, 액션 톤, 색·가독성, 시퀀싱.
- 산출물:
  - `STYLE_GUIDE.md`(8.3KB) — Horror(채도 0.3-0.5, emissive<0.5, bloom 0.2-0.35, Translucent, 절제) vs Action(채도 0.85+, HDR emissive 8-12, cubic ease 오버슈트 2.0-2.2x, frame0 동시 발사) + 색/가독성(그라데이션 매핑, MPC 색 토큰, 대비 4.5:1).
  - `SEQUENCING.md`(8.1KB) — 3단계(Anticipation→Burst→Linger) 단일 Timeline float 구동, 이벤트 서브이미터(CPU+Persistent IDs 필수), use* 토글, 호러 사망/액션 타격 예시 타임라인.
  - KNOWLEDGE cross-ref 갱신.

## Iteration 8 — 렌더 아트스타일 축 학습 (완료)
- 사용자 지적: 실사뿐 아니라 툰/스타일라이즈드도 학습 필요. 톤(분위기) 축과 직교하는 렌더-스타일 축 추가.
- 6에이전트 리서치: 실사, 툰/셀, 스타일라이즈드 핸드페인티드, 애니/가챠, 픽셀/기하.
- 산출물:
  - `STYLES.md`(10.6KB) — 스타일별 셰이딩/머티리얼(Lit vs Unlit·posterize·아웃라인·gradient map), 셰입/모션 언어, 텍스처 접근, 대표 레퍼런스.
  - `CODEX_PROMPTS.md`에 "Style Modifiers" 섹션 추가 — 스타일별 프롬프트 키워드 블록(기본 템플릿 뒤에 붙임): 실사/셀-flat-outline/핸드페인트 brush/크리스프 애니/픽셀.
  - KNOWLEDGE cross-ref 갱신(톤 축 ⊥ 스타일 축 명시).
- 규칙: 스타일 먼저 추론(기본=세미리얼) → 셰이딩+셰입+텍스처 한 세트 → codex에 스타일 키워드 append → 톤(STYLE_GUIDE 수치)과 직교 조합.

## Iteration 9 — 스타일별 codex 텍스처 실생성 검증 (완료)
- 동일 효과(매직 에너지 버스트)를 셀/핸드페인트/애니 modifier로 codex 3개 생성·육안 비교.
- 결과: 셋 다 명확히 구분됨 — 셀(하드 플랫 밴딩+볼드 아웃라인+기하), 핸드페인트(붓터치+따뜻한 채도+유기적), 애니(크리스프 그라데이션+글로시 블룸+룬 동심). 검은 배경 정확.
- 결론: 스타일 modifier 키워드 효과적 = 검증 완료. CODEX_PROMPTS Style Modifiers에 검증 노트 추가.

## Iteration 10 — 지식 베이스 정합성 점검·중복 정리 (완료)
- 9개 문서 헤딩·중복·모순 점검.
- 발견·수정:
  - 중복: KNOWLEDGE §4(효과 레시피 요약) ↔ RECIPES.md(상세) → KNOWLEDGE §4를 RECIPES 포인터+빠른 기준선으로 축약.
  - 모순(stale): KNOWLEDGE §4가 iter1 잔재로 "1024 8x8 flipbook" 기본 표현 → iter5 재정렬(머티리얼 모션 1순위, 플립북 틈새)·RECIPES no-flipbook과 충돌 → "구버전 flipbook 언급 무시" 명시 + 연출-우선 재기술.
  - 라우팅 부재: `INDEX.md` 신규 — 읽는 순서(INDEX→KNOWLEDGE→STYLES/STYLE_GUIDE 축→RECIPES→MF/SEQUENCING/CODEX), 문서 역할표, 절대 규칙.
  - 시스템 프롬프트: "KNOWLEDGE+CODEX 읽어라" → "INDEX 먼저 읽고 안내대로" 로 변경(재빌드 필요).
- emissive/bloom 수치(KNOWLEDGE Golden vs STYLE_GUIDE Horror/Action)는 맥락 차이(일반 PPV vs 톤별)로 모순 아님 — 유지.

## Iteration 11 — 효과군 확장 (완료)
- 6개 신규 효과(물/전기·번개/얼음·서리/바람·먼지/독·산/홀리·빛) 머티리얼+Niagara 연출 레시피 작성.
- RECIPES.md에 "확장 효과군" 섹션 추가(+35.7KB, 총 52KB) — 각 효과 Niagara 스택·노드그래프·age 커브·텍스처 입력(procedural/codex)·mcp 빌드 스텝. 모두 no-flipbook(panner/polar/dissolve/dynamic param).

## Iteration 12 — 게임 필 레이어 (완료)
- 4에이전트 리서치: SFX 트리거(Niagara Play Audio/MetaSound/attenuation), 카메라 셰이크(MatineeCameraShake 강도별), hit-stop/타임딜레이션·플래시.
- 산출물: `FEEL.md`(8KB) — impact frame stack(frame 0에 VFX+sound+shake+hitstop 동시), UE5 셋업, 강도별 프리셋 표(light/heavy/explosion/ambient). INDEX/역할표 갱신.
- 규칙: impact엔 sound+shake+hitstop 같은 프레임; ambient엔 shake/hitstop 금지; hitstop은 ms+SetTimer 복귀.

## Iteration 13 — 성능 검증 워크플로 (완료)
- 3에이전트 리서치: 측정(stat unit/GPU, ProfileGPU, Niagara Debugger, Insights, Quad Overdraw/Shader Complexity), GPU 비용 요인(오버드로/필레이트/머티리얼 명령수), 최적화 플레이북.
- 산출물: `PERF.md`(9KB) — measure→diagnose→fix 단계별: 측정 명령 → pass/fail 임계값(시스템당 CPU<1ms, GPU<5ms, 오버드로<4x, 머티리얼<200명령) → 증상→레버 진단표 → ROI순 수정 플레이북(스폰 감소/머티리얼 단순화/이미터 통합/거리 컬링/GPU 전환) → Effect Type 스케일러빌리티. INDEX/역할표 갱신.

## Iteration 14 — 공용 텍스처 라이브러리 사전 생성 (완료)
- codex로 6종 핵심 텍스처 병렬 생성 → `TextureLibrary/` + `MANIFEST.md`(용도·임포트 설정·연계 레시피):
  T_SoftGlow / T_SparkStreak / T_SmokePuff(grayscale) / T_FireWisp / T_MagicRunic(원형 룬 대칭 우수) / T_LightningArc(분기 아크 우수).
- 규칙: 이펙트 빌드 시 새 생성 전에 라이브러리 먼저 확인 → 있으면 /Game/VFX/Textures 로 임포트 재사용.

## Iteration 15 — 오블리비오 전용 호러 이펙트 3종 (완료, 사용자 지정 타깃)
- 사용자 레퍼런스(음파 공격 6레이어 컨셉시트) 기반 + 다크 오라 + 핏빛 할퀴기.
- 산출물: `OBLIVIO_EFFECTS.md`(18.9KB) — 효과별 레이어 분해표·Niagara 스택·머티리얼 그래프·age 커브·텍스처([procedural]/[library]/[codex])·호러 톤 수치·FEEL 페어링·빌드 스텝.
  - 음파: 7레이어(차지/링×3 staggered/리본/마이크로리플/먼지쇼크/잔류왜곡), 방향성 mesh ring(카메라 독립), 한랭 desaturated 톤.
  - 다크 오라: 빛 흡수(가산 금지), 검보라 wisp + fresnel sickly rim + 그라운드 tendril, 2-5s 슬로우.
  - 블러드 슬래시: 평행 클로 아크 + 임팩트 프레임만 brief flash(<0.1s) + 피 분사/얼룩, FEEL heavy-hit.
- INDEX에 프로젝트 전용 문서로 연결(우선 참조).

## 다음 반복 후보 (Iteration 16+)
- [ ] 레비가 OBLIVIO_EFFECTS 레시피로 실제 1개(추천: 다크 오라) 엔진 구축 → 스크린샷+PERF 검증. **남은 핵심 검증.**
- (크래시 수정 빌드 완료 — 이제 레비 스트리밍 안전. NS 열기 전 검증 규칙도 발효 중.)
- [ ] codex 프롬프트 A/B: 같은 효과를 프롬프트 변형으로 생성·비교해 템플릿 개선.
- [ ] 채널 패킹·플로우맵·모션벡터로 저프레임 플립북 보간 레시피 추가.
- [ ] 머티리얼 함수(Material Function) 템플릿화: soft particle, dissolve, panner, subUV 블렌딩.
- [ ] 호러/액션 톤에 맞춘 스타일 가이드(색·대비·연출) 별도 섹션.
- [ ] 검증 루프: 생성물 스크린샷 → 비평 → 지식/프롬프트 갱신.

> 각 반복은 KNOWLEDGE.md/CODEX_PROMPTS.md에 구체 스펙·템플릿을 추가/보정하고, 여기 결과를 한 줄로 남긴다.

## Iteration 16 — 소닉웨이브 튜토리얼 + Niagara 5.7 정확 스펙 (완료, 사용자 지정)
- 실제 에디터 로그에서 워닝 원인 실증: enum 핀에 표시명('Once')/숫자('2.0') 설정 → 엔진은 내부명(UserDefinedEnum NewEnumeratorX)만 파싱(GetValueByNameString, 엔진 소스 확인).
- 코드 수정: UNiagaraEmitterService::SetModuleInput에 enum 가드 — 표시명→내부명 자동 변환, 숫자/무효값 거부 + 가능값 목록 에러(셀프 코렉트 유도). [빌드 필요]
- 산출물: `NIAGARA_5_7.md`(7KB) — Emitter State/Spawn/Initialize Particle 정확 핀·enum 표(Lifetime=Initialize Particle float), enum 설정 규칙, LEGACY→현대 대체 표, 워닝→수정 표. INDEX 1순위 필수 문서로 연결.
- OBLIVIO_EFFECTS에 "Sonic Wave 기법 업그레이드" 추가(4.4KB) — 튜토리얼 실기법: 캡 없는 실린더 메시 링, Ring Pulse 머티리얼(RGB 3링 마스크+Motion_4WayChaos), 굴절 쇼크웨이브(index 1.5), 포스트프로세스 radial 왜곡, 다층 stagger.

## Iteration 17-18 — 워닝 정화 도구 + 실에셋 정화 검증 (완료)
- iter17: `repair_invalid_enum_pins(SystemPath)` 도구 추가(빌드 완료) — 깨진 enum 핀 자동 탐지, 표시명→내부명 구조, 무효값→모듈 기본값 리셋. NIAGARA_5_7.md에 사용법 기재.
- iter18: 헤드리스(UnrealEditor-Cmd) 정화 실행 — NS_SonicWave 수리+저장, 재검증 로드/컴파일에서 PinToNiagaraVariable 워닝 0건. 정화 완료.
- 워닝 3중 체계 완성: 예방(enum 가드) + 지식(NIAGARA_5_7 스펙) + 정화(repair 도구).
- 부수 발견: 프로젝트에 LFS 미체크아웃 의심 깨진 에셋 11개(BP_FlashLight/Mutant/사운드/맵 — "Invalid PACKAGE_FILE_TAG") — 팀 차원 git lfs pull 필요할 수 있음.

> 이 /loop(소닉웨이브 학습 + 워닝 개선) 목표 달성 — 종료. 새 워닝 패턴 발견 시 NIAGARA_5_7.md에 추가하는 방식으로 유지.

## Iteration 20 — 워크플로우 효율화: 조립-우선 FASTBUILD (완료, 사용자 지적)
- 문제: 레비가 매 이펙트마다 리서치→탐색적 파이썬→레벨 스폰→스크린샷→체크 풀 루프 — 느리고 비효율.
- 해법: "블록 조립 + 값 설정" 표준화.
  - `FASTBUILD.md` — 5단계 절차(레시피 1개 Read → 팔레트 copy_emitter 조립 → 값 설정 → compile+diagnostics → 보고). 금지: 단계별 스크린샷, 모듈 0부터, 문서 4+개, 탐색 전수조사.
  - `Content/Python/build_emitter_palette.py` — NS_EmitterPalette(/Game/VFX/_Library) 1회 구축 스크립트: EM_BurstSparks/ExpandingRing/RisingWisps/GlowCore/GroundDust/RibbonTrail 6블록(검증 모듈만, AddVelocity엔 Solve 동반).
  - INDEX 최상단 FASTBUILD 최우선 배너.
- 기존 copy_emitter/add_emitter API 활용 — 코드 변경/재빌드 없음, 즉시 발효.

## Iteration 21 — 원샷 빌더 levy_vfx (완료, 효율 심화)
- 병목 실증(세션 로그): 도구 실행은 총 25초(38회×0.6초)뿐 — 진짜 병목은 **호출 사이 LLM 왕복 46회**(왕복당 수 초~수십 초).
- 해법: 왕복 횟수 격감.
  - `Content/Python/levy_vfx.py` — spec(dict) 1개로 시스템 생성→팔레트 블록 조립→값/틴트/머티리얼→User 파라미터→컴파일→저장 일괄. 팔레트 자동 보장. `template` 키로 완성형 NS 복제 시작. 반환 로그의 FAIL 라인만 후속 수정.
  - FASTBUILD 개정: "이펙트 1개 = execute_python_code 1~3회" 목표 명시, spec 예시, discover_python_class 반복 금지, 템플릿 복제(NS_Template_* → _Library) 규칙.
- 기대: 호출 38회→1~3회 = 왕복 시간 ~90% 절감.

## Iteration 22 — 표준 에셋 내장 + Xociety 프로덕션 구조 학습 (완료, 사용자 지정)
- `build_vfx_assets.py` — 플러그인 내장(/Revvy/VFX/): 텍스처 6종 임포트(sRGB 규격) + 마스터 3종(M_LevyVFX_Additive / _Translucent / _Erosion) 노드 조립 + MI 6종 파생. 1회 실행, 재실행 안전.
- Xociety(사용자 이전 프로젝트, 2349 에셋) 4에이전트 마이닝 → `XOCIETY.md`: 25 NE 라이브러리 / 156 마스터+740 MI(MM→MI 위계, 효과군 폴더) / 파라미터 분포(Texture 2499·Mask 1666·Panner 278·Fresnel 265·Distortion 215·DepthFade 188·Erosion 147·Dissolve 126) / Legacy 격리 / 커스텀 모듈 4종.
- 채택 반영: 마스터 3호 M_LevyVFX_Erosion(Panner+Erosion — 프로덕션 최다 파라미터), 팔레트 +2(EM_ElectricArc/EM_MeshDebris → 8블록), MI-우선·마스터 수정 금지 거버넌스(FASTBUILD).

## Iteration 23-24 — CGHow 기법 + 텍스처·머티리얼 제작 심화 (완료, 사용자 지정)
- iter23 `CGHOW.md`(5.6KB): CGHow 300+ 튜토리얼 시그니처 — Noise→SmoothStep dissolve 게이트웨이, Curl Noise(~50)+Spring 유기 모션, Fountain 템플릿 베이스, 다층 staggered(0.1-0.3s) 히트 스택(코어/플래시/플레어/글로우/스트릭/굴절구/링), Additive Unlit 80%+, 속도분해→Lerp 반응형, 리본/스프라이트 색 분리.
- iter24 `TEXMAT.md`: 채널 패킹 규격(R=셰입/G=erosion/B=디테일/A=엣지, DXT5 1샘플러), 해상도 매트릭스, sRGB OFF 필수 목록, Panner 속도비(베이스:디테일 2:1), 그라데이션 LUT 256×8, flowmap [-1,1] RG, MI 생성 패턴, 마스터 아키텍처 결론.
- INDEX 역할표에 CGHOW/XOCIETY/TEXMAT 연결. 지식 베이스 17문서.

## Iteration 25 — Revvy 팩 학습 + 팔레트 완성 + 텍스처 플러그인 내장 (완료, 사용자 지정)
- 팩 2종 프로젝트 복사(Content/Niagara_Effects03 399M + StylizedFX_3 66M) 후 4에이전트 마이닝 → `REVVY_PACKS.md`:
  - NS 구조: 8-14 이미터 레이어링(코어 1-2/블룸 2-3/데브리·트레일 2-3/라이트 1-2/디스토션 1-2), SpawnBurst(순간)+SpawnRate(여운) 분리, Cylinder/Sphere Location 혼용, CurlNoise+Vortex+PointAttraction 합성, ColorFromCurve 필수(화이트 피크→색 페이드), 렌더러 믹스(Sprite+Mesh+Ribbon+Light).
  - 머티리얼 패밀리 12종 레시피(빔=TexCoord→Panner→Power, 플래시=DepthFade+Additive 7-8노드 미니멀, 디스토션=노멀+SphereMask...).
- 팔레트 NS_EmitterPalette **/Revvy 플러그인 내장 완성**(8블록, V2 InitParticle 단일·스폰 단일 검증). 빌더 4버그 수정(create_system /Game prepend 크래시·registry false-negative·필드명·중복 dedup).
- 팩 텍스처 15종 플러그인 추출(/Revvy/VFX/Textures — 참조 빈도 기반 선별: T_Noise_Vector 40회·T_Aura_GlowBase 38회 등). MANIFEST 갱신.

## Iteration 26 — 학습 실행형 가공: PLAYBOOK + 프리셋 (완료, 사용자 지정)
- 문제: 지식 18문서로 분산 — 레비 탐색 비용 증가, 리서치체 문서는 실행형 아님. 추가 버그: spec 예시 모듈명 "Initialize Particle"(공백)은 Contains 매칭 실패.
- 가공:
  - `levy_vfx.PRESETS` 6종(fire/impact_hit/smoke/sonic_wave/dark_aura/slash_trail) — 블록+값+MI 완성형, `build_preset(name, path, tint=...)` 1줄 빌드. CGHow 스택·Revvy 레이어링·OBLIVIO 수치 통합.
  - `PLAYBOOK.md` — 단일 실행 진입점: 프리셋→spec→머티리얼 선택표→레이어링 공식→톤 표→완료 기준. 80% 케이스 단독 커버.
  - INDEX 재편(PLAYBOOK 최우선), FASTBUILD 모듈명 교정(공백 없는 실제 함수명).
- 효과: 이펙트 표준 경로 = Read 1(PLAYBOOK) + execute_python 1(build_preset) = **왕복 2회**.

## Iteration 27 — add_module 스테이지 버그 근본 수정 (완료, 사용자 에러 리포트)
- 증상: 레비 생성 이펙트에 compile error 4건 — "Particle Update - Spawn Burst Instantaneous - Compile Error".
- 원인: AddModule이 "Spawn"/"Update" 짧은 이름 미인식 → silent 기본값 ParticleUpdate로 추락 → 이미터 스테이지 모듈(SpawnRate/SpawnBurst/EmitterState)이 파티클 스택에 들어감.
- 수정(빌드 완료): ①짧은 별칭 정식 인식 + 미인식 문자열은 명시 에러 ②모듈 ModuleUsageBitmask 기반 **스테이지 자동 보정**(비호환 요청 시 지원 스테이지로 리다이렉트+로그).
- 팔레트 재구축·검증: SpawnBurst/SpawnRate→EmitterUpdate, Init/AddVelocity→ParticleSpawn, Forces/Solve→ParticleUpdate 전부 정상. 저장 완료.
- 잔여: 사용자가 이전에 만든 에러 NS는 재생성 필요(레비가 다시 빌드하면 자동 보정 적용).

## Iteration 28 — 플러그인 독립화: Revvy 리네임 + Mixamo 제거 + 전 에셋 내장 (완료, 사용자 지정)
- 플러그인 Revvy→Revvy 리네임(폴더/uplugin/FriendlyName/마운트 /Revvy/, 16파일 동기화). 내부 모듈명 Revvy 유지(코드/MCP 안정).
- Mixamo 임베드 제거 — RevTools 의존 완전 분리(uplugin/Build.cs/SAIChatWindow 원복). 단일 플러그인 폴더로 이식 가능.
- VFX 표준 에셋 전부 .uasset로 플러그인 영구 내장: 마스터 3 + MI 6 + 텍스처 21 + 팔레트 8블록 — 새 프로젝트에서 생성 절차 불필요(스크립트는 복구용).
- INSTALL.md — 3단계 설치 가이드(복사→빌드→활성화).

## Iteration 29 — RealisticBlood + OutlineMaterial 학습·내장 (완료, 사용자 지정)
- 팩 복사: Content/RealisticBlood(348, 효과군 11종 NS 37개) + Content/OutlineMaterial(183).
- `REALISTIC_BLOOD.md`: 4레이어 블러드 표준(Burst 0.1-0.5s → 포물선 드롭릿(gravity+drag) → CPU Collision 데칼 고임(방향성 MI 21종 매칭) → 드립 잔류 0.5-3s), 마스터 2종(M_LiquidBase/M_LiquidDecalBase)→MI 46 위계, 웻룩(다크레드+노멀+리플렉션), LOD High/Med/Low 티어.
- `OUTLINE.md`: 아웃라인 기법 — 툰/셀 스타일 축 연계.
- 블러드 텍스처 12종 플러그인 추출(스플래터 코어+노멀/방향성/고어/데칼/퍼들/드롭릿/액체 노멀·리플렉션). OBLIVIO 블러드 슬래시 레시피에 4레이어 표준 + 템플릿 복제 가이드 반영. INDEX/MANIFEST 갱신.



---

## [복구 사고 기록] 2026-06-14 — Plugins/Revvy 워킹트리 삭제 + 부분 복구
- **사고**: 파괴적 git 명령(clean -ffdx 류)으로 Plugins/Revvy working tree 전체 삭제(.git/objects만 잔존, 손상). 에디터서 플러그인·채팅창 안 뜸.
- **복구**: 원격 RevStudio/Revvy.git(main 8b0bbf6) 풀 클론 → Plugins/Revvy 복원 + OblivioEditor 리빌드(101/101, DLL OK). .git/objects 백업 C:/Unreal/_revvy_git_backup.
- **소실 확정**: git 미커밋(아크 내내 "허락 없이 커밋 금지") + 트랜스크립트 미보존 + dangling commit/blob 0 + 디스크 사본 0 → **NIAGARA_MAKING §1~25(2535줄)·LOG 지식누적 iter1~6 영구 소실**.
- **이 대화서 재생성 복원**: RECIPE_LIBRARY.md(효과 18종+부록4, Epic 공식 검증값 verbatim)·MATERIAL_FUNCTIONS.md(검증 노드체인 부록 12종)·NIAGARA_MAKING.md(§26+복구노트)·INDEX 배선. → **Revvy git 커밋·푸시**(이번엔 사용자 허락).
- **교훈**: 장기 지식누적은 주기적 커밋·푸시 필수("커밋 금지" 제약이 단일 working-tree 유실에 취약). 이후 의미있는 누적분은 커밋 권장.
