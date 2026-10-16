"""② 느린 이동 · ③ 회전 안 됨 — 두 가지를 한꺼번에 설명할 후보를 본다.

## 의심 ① 루트모션이 캡슐 속도를 덮어쓴다

`ABP_Customer` 의 루트모션 모드가 `RootMotionFromMontagesOnly` 다. 그런데 §9-2
에서 로코모션을 **몽타주로** 바꿨다. 만약 `Walk_Fwd_Manny` 에 `EnableRootMotion`
이 켜져 있으면 —

    몽타주에 루트모션이 있다 -> AnimInstance 가 그걸 뽑는다
    -> CharacterMovement 가 **속도를 루트모션으로 덮어쓴다**
    -> MaxWalkSpeed(350) 는 무시되고 **애님의 전진량**이 속도가 된다

그런데 그 애님은 `scale_horizontal=0` 으로 **제자리로 구웠다** (전진 0.00cm).
즉 루트모션이 0 이라 **거의 안 움직인다.**

⚠ 리타게터가 소스(`A_WalkFwd_Loop`, EnableRootMotion=True)의 플래그를 **그대로
  복사했을 수 있다.** 그러면 이 함정에 정확히 걸린다.

## 의심 ② `ClaimedBy` 가 캐시라 안 채워져 있다

`FaceClaimedSlot` 은 `S->ClaimedBy.Get() == C` 로 슬롯을 찾는다. 그런데 헤더가
「잡은 액터. **진실은 Subsystem** 이고 이건 캐시다」라고 적어 놨다. 캐시가 안
채워지면 슬롯을 못 찾고 — 경고만 찍고 **회전을 건너뛴다.**
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

L('=== ① 루트모션 ===')
abp = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/ABP_Customer')
cdo = unreal.get_default_object(abp.generated_class())
L('  ABP_Customer.root_motion_mode = %s' % cdo.get_editor_property('root_motion_mode'))

L('')
L('  -- 시퀀스의 EnableRootMotion --')
for n in ('Walk_Fwd_Manny', 'Idle', 'Walking',
          'Reaching_Out', 'Eating_a_Burger', 'SitIdle_Male'):
    a = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Anims/%s' % n)
    if not a:
        continue
    L('    %-18s enable_root_motion=%-6s force_root_lock=%-6s'
      % (n, a.get_editor_property('enable_root_motion'),
         a.get_editor_property('force_root_lock')))

L('')
L('  -- 소스(마네퀸) 비교 --')
src = EAL.load_asset('/Game/Characters/Mannequins/Anims/Unarmed/RetargetWalk/A_WalkFwd_Loop')
if src:
    L('    %-18s enable_root_motion=%-6s force_root_lock=%s'
      % ('A_WalkFwd_Loop', src.get_editor_property('enable_root_motion'),
         src.get_editor_property('force_root_lock')))

L('')
L('  -- 몽타주가 루트모션을 들고 있나 --')
for n in ('AM_Loco_Walk', 'AM_Loco_Idle', 'AM_Loco_Sit', 'AM_Reaching_Out'):
    m = EAL.load_asset('/Game/BurgerHouse/Anims/Montages/%s' % n)
    if not m:
        continue
    vals = []
    for p in ('has_root_motion', 'enable_root_motion', 'b_enable_root_motion',
              'enable_root_motion_translation', 'enable_root_motion_rotation'):
        try:
            vals.append('%s=%s' % (p, m.get_editor_property(p)))
        except Exception:
            continue
    L('    %-18s %s' % (n, ' · '.join(vals) if vals else '(관련 프로퍼티 없음)'))

L('')
L('  ⚠ `enable_root_motion=True` 인 시퀀스가 로코모션 몽타주에 들어 있으면')
L('    RootMotionFromMontagesOnly 모드에서 **캡슐 속도가 무시된다.**')

# ─────────────────────────── ②
L('')
L('=== ② ClaimedBy 가 실제로 채워지나 ===')
L('  `FaceClaimedSlot` 은 ASlotPoint::ClaimedBy 로 슬롯을 찾는다.')
L('  ⚠ 헤더가 「진실은 Subsystem 이고 이건 캐시」라고 적어 놨다 —')
L('    캐시를 안 채우면 못 찾고 회전을 건너뛴다 (경고만 찍고).')
L('')
L('  -> StoreSubsystem::ClaimSlot 이 ClaimedBy 를 쓰는지는 소스로 확인할 것.')
L('     (런타임 값은 PIE 없이는 못 본다)')

L('')
L('=== ③ 참고: BP_Customer 의 이동 값 ===')
bc = unreal.get_default_object(
    unreal.load_object(None, '/Game/BurgerHouse/BP/BP_Customer.BP_Customer_C'))
mv = bc.get_editor_property('character_movement')
for p in ('max_walk_speed', 'max_acceleration'):
    L('  %-24s %s' % (p, mv.get_editor_property(p)))
L('  montage_locomotion       %s' % bc.get_editor_property('montage_locomotion'))
