"""`Sit_Idle` 이 실제로 **어떻게** 루프하는지 값으로 확인한다. 안 고친다.

루프는 몽타주 설정이 아니라 **C++ 타이머**가 만든다
(`ACustomerCharacter::PlayLoopedAction`) —

    Len  = Montage_Play(M)                       한 번 재생 (루프 플래그 없음)
    Next = max(Len - DecorationLoopCrossfade, 0.25)
    타이머 Next 초 뒤에 PlayLoopedAction 다시

그래서 **몽타주 자체의 루프 횟수는 1**이어도 화면에서는 끝없이 돈다.
여기서는 그 값들이 실제로 얼마인지, 재생 주기가 몇 초인지 찍는다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP_PATH = '/Game/BurgerHouse/BP/BP_Customer'
MTG = '/Game/BurgerHouse/Anims/Montages/AM_SitIdle_Male'

cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP_PATH))

L('=== ① BP_Customer 의 장식 루프 값 ===')
vals = {}
for p in ('decoration_loop_crossfade', 'decoration_gap', 'decoration_single_action',
          'decoration_seed'):
    try:
        v = cdo.get_editor_property(p)
        vals[p] = v
        L('  %-28s %s' % (p, v))
    except Exception as ex:
        E('  %-28s 못 읽음: %s' % (p, str(ex)[:60]))

L('')
L('=== ② AM_SitIdle_Male 자체의 루프 설정 ===')
m = EAL.load_asset(MTG)
dur = m.get_editor_property('sequence_length')
L('  길이 %.3fs' % dur)
tr = m.get_editor_property('slot_anim_tracks')[0]
seg_info = tr.get_editor_property('anim_track')
for p in ('anim_segments',):
    try:
        segs = seg_info.get_editor_property(p)
        for i, s in enumerate(segs):
            L('  세그먼트[%d] loop_count=%s start=%.3f end=%.3f play_rate=%.2f'
              % (i, s.get_editor_property('loop_count'),
                 s.get_editor_property('anim_start_time'),
                 s.get_editor_property('anim_end_time'),
                 s.get_editor_property('anim_play_rate')))
    except Exception as ex:
        # ⚠ UE 5.8 의 `AnimSegment` 에는 `loop_count` 가 파이썬으로 안 나온다.
        #   문제는 아니다 — 루프는 애초에 몽타주가 아니라 C++ 타이머가 만든다.
        L('  세그먼트 세부는 파이썬에 안 나온다 (%s) — 루프와 무관하다'
          % str(ex).split(':')[0])

for p in ('b_enable_auto_blend_out', 'enable_auto_blend_out', 'blend_out_trigger_time'):
    try:
        L('  %-28s %s' % (p, m.get_editor_property(p)))
    except Exception:
        continue

L('')
L('=== ③ 실제 재생 주기 ===')
cf = vals.get('decoration_loop_crossfade', 0.35)
gap = vals.get('decoration_gap', 3.0)
nxt = max(dur - max(cf, 0.0), 0.25)
L('  Len %.3fs · Crossfade %.2fs  ->  다음 재생까지 %.3fs' % (dur, cf, nxt))
L('  즉 %.2f초마다 다시 걸고, 마지막 %.2f초가 겹친다 (분당 %.0f회)'
  % (nxt, min(cf, dur), 60.0 / nxt))
L('  첫 재생은 0~%.1fs 사이로 흩어진다 (DecorationGap) — 다섯 명이 동시에 안 움직인다'
  % gap)

L('')
L('=== ④ 이 크로스페이드가 아직 필요한가 ===')
BONES = ('Hips', 'Spine', 'Spine1', 'Neck', 'Head', 'LeftArm', 'LeftForeArm',
         'RightArm', 'RightForeArm', 'LeftUpLeg', 'LeftLeg', 'RightUpLeg', 'RightLeg')
seq = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Anims/SitIdle_Male')
d = seq.get_editor_property('sequence_length')
total = 0.0
for b in BONES:
    r0 = unreal.AnimationLibrary.get_bone_pose_for_time(seq, b, 0.0, False).rotation.rotator()
    r1 = unreal.AnimationLibrary.get_bone_pose_for_time(seq, b, d - 1e-4, False).rotation.rotator()
    total += abs(r1.pitch - r0.pitch) + abs(r1.yaw - r0.yaw) + abs(r1.roll - r0.roll)
L('  첫↔끝 자세 차이 %.1f도' % total)
L('  (먹는 애님은 221.0도였다 — 크로스페이드는 그걸 덮으려고 넣은 것이다)')
