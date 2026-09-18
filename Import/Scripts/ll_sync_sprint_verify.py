"""속도 동기화 + 버거집 스프린트 차단을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).
⚠ 입력 차단의 **실제 동작**은 여기서 못 본다 — PIE 에서 눌러 봐야 한다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

CUST = '/Game/BurgerHouse/AI/BP_Customer'
PLAYER = '/Game/MetaHumans/MHC_Character_Adam/Cheon/BP_CHeon'
fails = []

# ---- ① 속도 동기화 ----
L('=== ① 속도 동기화 ===')
pc = unreal.get_default_object(unreal.load_object(None, '%s.BP_CHeon_C' % PLAYER))
walk = pc.get_editor_property('walk_speed')
sprint = pc.get_editor_property('sprint_speed')
pmv = pc.get_editor_property('character_movement')

cc = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % CUST))
cmv = cc.get_editor_property('character_movement')

L('  %-28s %10s %10s' % ('', '플레이어', '손님'))
L('  %-28s %10.1f %10.1f' % ('WalkSpeed / MaxWalkSpeed', walk,
                             cmv.get_editor_property('max_walk_speed')))
L('  %-28s %10.1f %10.1f' % ('MaxAcceleration',
                             pmv.get_editor_property('max_acceleration'),
                             cmv.get_editor_property('max_acceleration')))
L('  %-28s %10.1f %10.1f' % ('BrakingDecelWalking',
                             pmv.get_editor_property('braking_deceleration_walking'),
                             cmv.get_editor_property('braking_deceleration_walking')))
L('  (플레이어 SprintSpeed %.1f — 손님은 여기 안 맞춘다)' % sprint)

got = cmv.get_editor_property('max_walk_speed')
if abs(got - walk) > 0.5:
    fails.append('손님 속도 %.1f 가 플레이어 걷기 %.1f 와 다르다' % (got, walk))

# ---- ② 스프린트 차단 ----
L('')
L('=== ② 스프린트 차단 (C++ 기본값) ===')
try:
    cpp = unreal.get_default_object(unreal.FebriculaCharacter.static_class())
    blocked = [str(x) for x in cpp.get_editor_property('sprint_blocked_levels')]
    L('  AFebriculaCharacter.SprintBlockedLevels = %s' % blocked)
    if 'L_BurgerHouse' not in blocked:
        fails.append('C++ 기본값에 L_BurgerHouse 가 없다')
except Exception as ex:
    E('  프로퍼티를 못 읽는다 — 빌드 확인: %s' % str(ex)[:70])
    fails.append('SprintBlockedLevels 프로퍼티가 없다')

# 플레이어 폰 BP 가 그 값을 덮었는지 (덮으면 차단이 풀린다)
try:
    pblocked = [str(x) for x in pc.get_editor_property('sprint_blocked_levels')]
    L('  BP_CHeon 의 값                        = %s' % pblocked)
    if 'L_BurgerHouse' not in pblocked:
        fails.append('BP_CHeon 이 목록을 덮어써서 L_BurgerHouse 가 빠졌다')
except Exception as ex:
    E('  BP_CHeon 에서 못 읽음: %s' % str(ex)[:70])

# ---- ③ 레벨 이름이 실제로 그 문자열인가 ----
L('')
L('=== ③ 레벨 이름 대조 ===')
L('  판정은 UGameplayStatics::GetCurrentLevelName(..., true) 와 비교한다.')
L('  L_BurgerHouse.umap 의 에셋 이름: %s'
  % unreal.EditorAssetLibrary.load_asset('/Game/BurgerHouse/Maps/L_BurgerHouse').get_name())
L('  ⚠ 경로가 아니라 **이름**이라야 맞는다. PIE 접두사는 그 함수가 뗀다.')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 ===')

L('')
L('=== ⚠ PIE 에서 확인할 것 ===')
L('  1) L_BurgerHouse 에서 스프린트 키를 눌러도 **속도가 안 오른다**')
L('  2) 다른 레벨(예: Lvl_ThirdPerson)에서는 **여전히 1000 으로 뛴다**')
L('  3) 손님과 플레이어가 나란히 걸을 때 **속도가 같아 보인다**')
L('  4) 손님 발 미끄러짐 — 애님 183 vs 캡슐 500 (2.73배)')
