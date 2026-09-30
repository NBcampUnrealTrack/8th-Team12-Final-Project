"""버거집 손님에 쓸 **앉은 몸짓 후보**를 전부 재 본다. 안 고친다.

## 무엇을 보나 — 「앉은 동작인가」가 첫 관문이다

이름에 `Sitting`·`SitIdle` 이 들어가도 실제로는 **서 있는 동작**인 것이 섞여
있다. 문서 §7 이 두 번 데인 지점이다 (`AM_Sit_Texting` 등). 판정은 골반 Z —

    앉음 57~63   ·   섬 ~103

그리고 **루프 이음매**(첫↔끝 자세 차이)도 같이 잰다. 장식 손님은 이 동작을
끝없이 반복하므로, 이음매가 큰 것은 크로스페이드로 덮어야 한다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

MIX = '/Game/BurgerHouse/Anims/Mixamo/Anims'
CHAIR_M = '/Game/Characters/ChairAnimation/Male'
CHAIR_F = '/Game/Characters/ChairAnimation/FeMale'

# 손님 스켈레톤에 **이미 올라와 있는** 것 — 리타겟 불필요
MIXAMO = [
    'Sitting_Talking', 'Sitting_Thumbs_Up', 'Sitting_Yell',
    'Sitting', 'Talking_On_A_Cell_Phone', 'Texting',
    'Idle', 'SitIdle_Male',
    'Eating_a_Burger', 'Eating_Fries', 'Eating_a_Drumstick', 'Eating_Frantically',
]

# 마네퀸 — 쓰려면 `ll_sit_retarget.py` 로 옮겨야 한다
CHAIR = [
    (CHAIR_M, 'SitIdle_Male'), (CHAIR_M, 'SitIdle_UpHand_Male'),
    (CHAIR_M, 'SitIdle_UpHand_SpineHead_Male'), (CHAIR_M, 'SitIdle_UpHandHead_Male'),
    (CHAIR_M, 'SitIdle_Sleep_Male'),
    (CHAIR_F, 'SitIdle_FeMale'), (CHAIR_F, 'Sit_Idle_Think_FeMale'),
    (CHAIR_F, 'Sit_Idle_UpHand_FeMale'), (CHAIR_F, 'Sit_Idle_Write_FeMale'),
]

MIX_BONES = ('Hips', 'Spine', 'Spine1', 'Neck', 'Head', 'LeftArm', 'LeftForeArm',
             'RightArm', 'RightForeArm', 'LeftUpLeg', 'LeftLeg', 'RightUpLeg', 'RightLeg')
MAN_BONES = ('pelvis', 'spine_01', 'spine_03', 'neck_01', 'head',
             'upperarm_l', 'lowerarm_l', 'upperarm_r', 'lowerarm_r',
             'thigh_l', 'calf_l', 'thigh_r', 'calf_r')


def rot(a, b, t):
    return unreal.AnimationLibrary.get_bone_pose_for_time(a, b, t, False).rotation.rotator()


def report(path, bones, pelvis):
    if not EAL.does_asset_exist(path):
        E('  %-34s 없다' % path.split('/')[-1])
        return
    a = EAL.load_asset(path)
    dur = a.get_editor_property('sequence_length')
    p0 = unreal.AnimationLibrary.get_bone_pose_for_time(a, pelvis, 0.0, False)
    z = p0.translation.z

    seam = 0.0
    amp = 0.0
    for b in bones:
        try:
            r0, r1 = rot(a, b, 0.0), rot(a, b, dur - 1e-4)
        except Exception:
            continue
        seam += abs(r1.pitch - r0.pitch) + abs(r1.yaw - r0.yaw) + abs(r1.roll - r0.roll)
        vals = [rot(a, b, min(dur * i / 12.0, dur - 1e-4)) for i in range(13)]
        amp += max(max(getattr(v, k) for v in vals) - min(getattr(v, k) for v in vals)
                   for k in ('pitch', 'yaw', 'roll'))

    pose = '앉음' if z < 70 else ('섬' if z > 90 else '중간')
    L('  %-34s %6.2fs  골반Z %6.1f %-4s  움직임 %6.0f도  이음매 %7.1f도'
      % (path.split('/')[-1], dur, z, pose, amp, seam))


L('=== 손님 스켈레톤에 이미 있는 것 (리타겟 불필요) ===')
L('  %-34s %6s  %-13s  %-13s %s' % ('', '길이', '골반', '움직임', '이음매'))
for n in MIXAMO:
    report('%s/%s' % (MIX, n), MIX_BONES, 'Hips')

L('')
L('=== 마네퀸 ChairAnimation (쓰려면 리타겟 필요) ===')
for d, n in CHAIR:
    report('%s/%s' % (d, n), MAN_BONES, 'pelvis')

L('')
L('⚠ 「섬」으로 나온 것을 전신으로 틀면 앉은 손님이 **일어선다** (§7).')
L('⚠ 이음매가 큰 것은 DecorationLoopCrossfade 0.35 로 덮인다 — 못 쓰는 건 아니다.')
