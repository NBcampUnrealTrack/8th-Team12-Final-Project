# NIAGARA_MAKING.md — Niagara 제작 종합 가이드 (UE5.5+ 누적)

> ⚠️ **2026-06-14 부분 복구 안내**: 워킹트리 삭제 사고로 §1~25(2535줄, 이전 세션 산물)는 **영구 소실**(git 미커밋 + 트랜스크립트 미보존 → 복구 불가). 아래 §26(이번 세션 검증분)만 보존. §1~25 핵심은 RECIPE_LIBRARY.md(효과별 레시피 18종+부록), MATERIAL_FUNCTIONS.md(노드체인 12종 검증), NIAGARA_5_7.md(5.7 핀/enum), KNOWLEDGE.md(우선순위·골든넘버)에 상당 부분 중복 보존됨. §1~25 재작성은 별도 /loop로 재누적 가능.
>
> **제1원칙(보존)**: 불/연기 = 움직이는 입자❌, **애니 머티리얼 입힌 정지 스프라이트✅**(Sword_flames01 해체 검증). SubUV = 렌더러 SubImageSize + SubUVAnimation Linear + 머티리얼 TexCoord0 샘플.

---

## §26 고급 시스템 검증 부록 (Epic 공식 + adversarial, UE5.5+)

> Workflow 6렌즈(research→adversarial). 3 high-correct + 3 교정반영(stateless CPU/Beta·scratch If값-select·neighbor 함수명).

### §26-1 Simulation Stages + Grid2D/3D Collection (검증 high)
- **메커니즘**: GPU 추가 컴퓨트 패스(Particle Update 후). **Iteration Source(ENiagaraIterationSource: Particles/DataInterface/DirectSet)** — DataInterface 모드=셀당 1스레드(512²=262144). 셀좌표 `ExecutionIndexToGridIndex`(IndexX/Y/Z)·`ExecutionIndexToUnit`(0~1 UV). **읽기=Previous 버퍼(SamplePreviousGrid*)·쓰기=Current(StackContext Set*)** — Grid는 **자동 더블버퍼(ping-pong)**=레이스 방지.
- **3스테이지 표준**: ①Initialize(Emitter Reset Only)②Solve(Iterations>1=확산/압력 relaxation)③WriteToTexture(RenderTarget2D, SetRenderTargetValue).
- **필수**: **Sim Target=GPUCompute Sim**(grid DI·sim stage는 GPU전용·CPU 무음), **Calculate Bounds=Fixed**, 해상도 Emitter Spawn서 비0 설정(0=0스레드 무동작). Grid2D DI: Num Cells X/Y·World BBox Size·Override Buffer Format(Half Float 싸게/Float 정밀). 출력은 **Render Target User Parameter로만 GPU 밖으로**(CPU 셀 직접접근 불가).
- **perf**: 512² solve <1ms, Grid3D는 큐빅(128³ 무거움). Niagara Fluids 플러그인=Grid 2D Set Resolution 등 편의모듈.

### §26-2 Lightweight(Stateless) 이미터 (검증 med — 교정 반영)
- **메커니즘**: 스크립트 無·**클로즈드폼 분석식**(상태 = 스폰파라미터+age 함수, 영속 어트리뷰트 버퍼 없음→틱 비용 격감). 컴파일 불요(즉시 반영).
- **⚠️교정(중요)**: **UE5.5서 CPU 전용**(GPU 스테이트리스 지원은 이후 버전 추가) — Sim Target 선택 불가. 상태 = **5.5서 Beta**(Experimental 아님). **5.4 도입**.
- **지원 ~17모듈만**: Acceleration Force·Add Velocity·Curl Noise/Noise Vector Field·Drag·Gravity Force·Initialize Particle·Initial Mesh Orientation·Rotate Around Point·Scale Color·Scale Mesh Size(/By Speed)·Scale Sprite Size(/By Speed)·Shape Location·**Solve Velocities And Forces**·Sprite Rotation Rate·Sub UV Animation. **커스텀 모듈/스크래치/Dynamic Input 불가, 충돌/이벤트/DI/어트리뷰트read 불가.**
- **언제**: 대량 단순 cosmetic(스파크/먼지/엠버/모트/분수). perf 이득은 **시스템 전체가 스테이트리스일 때 최대**(하나라도 stateful 섞이면 그 이미터 틱비용 부활).

### §26-3 Niagara Data Channels (NDC) (검증 high)
- **메커니즘**: 생산자↔소비자 디커플 명명 데이터스트림. **5.5 Production-Ready**(5.3/5.4 Experimental). 채널타입 **Global**(전역) vs **Islands**(그리드 큐브 분할, 먼 섬 미틱/미렌더=스케일).
- **생산**: BP/C++(NDC 라이브러리)·PCG(Write to NDC 노드)·다른 시스템(Niagara Data Channel Write DI). **소비**: Niagara Data Channel Read DI.
- **⚠️핵심 제약**: **GPU 이미터는 READ만, WRITE 불가**(쓰는 이미터=CPUSim). **같은 프레임 안 보임**(최소 다음 tick group). **Read Previous Frame**: true=+1프레임 지연·무손실 / false=0지연·늦은 write 놓칠 수.
- **리더 패턴**: Emitter Update에 **Spawn Conditional**(매칭 NDC 요소당 스폰, 필터 가능, `Engine.Emitter.SpawnGroup` 자동세팅) + Particle Spawn에 **Read**(SpawnGroup 인덱스로 payload→particle attr). 리스너=Loop Infinite + **Complete if Unused**.
- **⚠️함정**: Search Params 기본 Location=0 → Islands서 전부 원점섬(컬링). **bOverrideLocation+Location 또는 OwningComponent 매 write 설정**. 변수명 오타=무음 실패. (5.7은 Access Contexts로 리워크 — 5.5/5.6은 FNiagaraDataChannelSearchParameters+visibility bool).
- **언제**: 多 독립 소스→1 공유 이펙트(수백 탄착/발소리/히트), PCG 스캐터, 크로스시스템. 1회성=그냥 SpawnSystemAtLocation.

### §26-4 Data Interfaces (Skeletal/Static Mesh·Collision·Audio Spectrum) (검증 high)
- **메커니즘**: UNiagaraDataInterface(UObject)=외부 데이터 브릿지. User param(런타임 오버라이드) 또는 모듈로컬. **DI는 컴포넌트당 UObject 복제 → Emitter/System 스크립트서 읽고 분배**(per-particle 금지=비용).
- **Skeletal Mesh DI**: Skinning Mode=**Skin On The Fly**(기본), Sampling Regions(빈=전체)·Whole Mesh LOD(-1)·**Filtered Sockets/Bones**(GetFilteredSocketAt/RandomFilteredSocket)·Source Mode·Exclude Bone Name. ⚠️소켓-로컬은 DIY(매 Update 재read=velocity 죽음). per-bone 이미터=드로콜 폭증(30본×3=90).
- **Static Mesh DI**: Source Mode·LOD Index·Section Filter·**Allow Sampling From Streaming LODs**(끄면 LOD 스트림시 스냅)·**Capture Transforms Per Frame**(움직이는 메시 velocity).
- **Collision(GPU)**: **Depth Buffer**(스크린스페이스·오프스크린 놓침) / **Distance Fields**(Global SDF, **Project Settings Generate Mesh Distance Fields 필수+재시작**) / **GPU Ray Traces**(Experimental·DX12+HW RT·1프레임 지연). CPU Analytical=평면/구 한정. Resilience·Friction.
- **Audio Spectrum DI**: Submix FFT→GPU 버퍼. Resolution·Min/Max Frequency·Noise Floor Db, `GetSpectrumValue()`/`GetNumChannels()`. ⚠️Submix에 오디오 실제 라우팅 필요(아니면 무음). (메시샘플+오디오스펙트럼 스크래치 동시 = 일부 5.x서 PIE 크래시 이력).

### §26-5 Scratch Pad 모듈 (검증 — 1건 교정)
- **메커니즘**: 에셋 안 자립 그래프. **Map Get(빨강 Input Map, 읽기)→Parameter Map 와이어→로직/CustomHLSL→Map Set(초록 Output Module, 쓰기)**. 암묵 전달 無(Parameter Map에 없으면 존재 안 함).
- **네임스페이스**: PARTICLES.(영속)·MODULE.(=INPUT., 스택 Details 노출)·OUTPUT.(같은 스테이지 후속 모듈)·LOCAL./TRANSIENT.(비영속)·ENGINE.(읽기전용)·EMITTER./SYSTEM.·USER.(BP/시퀀서 구동).
- **⚠️교정(중요)**: **If/Select 노드 폐기 아님**(loadable·supported). 단 **값-select(양쪽 다 평가 후 택1)=제어흐름 아님**(스킵/단락 불가). **진짜 분기=CustomHLSL, 컴파일타임 선택=Static Switch.** (기존 KB "If 폐기" 서술 정정).
- **함정**: **Apply 눌러야 라이브 반영**. 'Show Parameter Writes'(gear)로 Map Set 확인. 타입 정확 일치(Vector↔Position 변환 필요). CustomHLSL=Niagara HLSL 방언(머티리얼 아님). 'Create Asset'으로 NMS_ 승격(이후 시스템 밖, 재적용 필요).

### §26-6 GPU Neighbor Grid 3D + Particle Attribute Reader (검증 — 90%, 교정 반영)
- **메커니즘**: 입자-입자 공간쿼리 O(neighbors)(전수 O(N²) 회피). **Neighbor Grid 3D**=입자를 복셀에 인덱스 버킷팅(어트리뷰트 저장 안 함, **인덱스만**) + **Particle Attribute Reader**=이웃 실제 attr 읽기. 둘 다 GPU전용+멀티 sim stage.
- **3스테이지**: Reset(클리어)→Populate/Register(Iteration=Particles, 각 입자 셀에 Execution Index 기록)→Query(자기+인접 26셀 인덱스 read→Attribute Reader 'Get <Type> By Index'로 이웃 Position/Velocity→분리/정렬/응집 등 누적). 이웃 루프는 보통 **Scratch/CustomHLSL**.
- **Attribute Reader**: **System 레벨 Attribute Reader 변수→Emitter Name 바인드**. 'Get <Type> By Index/ID'·'Get Num Particles'·'Get ID At Spawn'. **읽기 1프레임 지연**. 인덱스는 >64 입자서 매프레임 repack→불안정 → **stable ID('Get ID At Spawn'+Requires Persistent IDs)** 권장. CPU↔GPU read 실패.
- **⚠️교정**: ①Set Resolution(Set Num Cells) 배치는 **Emitter Spawn / System Update 둘 다 유효**(AddedToGrid=false는 UE5.0 버그·5.1+ 해결). ②실제 HLSL 함수명 = **GetNumCells·SetNumCells·UnitToIndex·IndexToLinear·NeighborGridIndexToLinear·SetParticleNeighborCount·MaxNeighborsPerCell·SetParticleNeighbor·GetParticleNeighbor·SimulationToUnit**("NeighborGridCell"은 날조). ③`GetCellSize()` 버그(0 반환)→`WorldGridExtent/GetNumCells()` 수동.
- **Max Neighbors Per Cell=하드캡**(오버플로 무음 드롭). **1000+ 입자서만 가치**(GPU warp: 1입자=64입자 비용). 단일 리더(트레일/체인)=Attribute Reader 단독(그리드 불요). 환경충돌=GPU Collision. 필드=Grid 2D/3D Collection.

**출처(검증)**: Epic 공식 docs(Simulation Stages·Grid Collection·niagara-lightweight-emitters-overview·niagara-data-channels-overview·Write/ReadFromNiagaraDataChannel·Data Channels 5.7 Access Contexts·SkeletalMesh/StaticMesh/AudioSpectrum DI API·gpu-raytracing-collisions·niagara-scratch-pad-modules·scalability-and-best-practices) + ibbles/LearningUnreal(Attribute Reader) + realtimevfx + forums(NeighborGrid3D 5.0 changes·GetCellSize 버그). 6렌즈 adversarial.
