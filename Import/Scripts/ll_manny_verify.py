"""리타겟 결과가 **진짜 움직이는지** 잰다 — 새 프로세스로 돌릴 것.

## ⚠⚠ 스켈레톤만 보면 안 된다

op 이 하나도 없는 리타게터는 **타깃 레퍼런스 포즈를 그대로 뱉는다.** 그래도
에셋은 생기고 스켈레톤·길이·트랙 수가 전부 맞아서 어디서도 안 걸린다 (§7).
그래서 **본이 실제로 얼마나 움직이는지**를 본다.
"""
import unreal
import math

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

OUT = '/Game/BurgerHouse/Anims/Manny'
OLD = '/Game/BurgerHouse/Anims/Mixamo/Anims'

# 마네퀸 본 / Mixamo 본 — 같은 자리를 가리키는 짝
PAIRS = [('pelvis', 'Hips'), ('spine_03', 'Spine1'), ('upperarm_l', 'LeftArm'),
         ('hand_l', 'LeftHand'), ('thigh_l', 'LeftUpLeg'), ('foot_l', 'LeftFoot')]

CHECK = ['Walk_Fwd_Manny', 'Idle', 'SitIdle_Male', 'Reaching_Out', 'Carrying',
         'Eating_a_Burger', 'Sitting_Talking', 'Texting']

MIN_DEG = 8.0      # 도 — 어떤 본도 이만큼 안 돌면 레퍼런스 포즈다
fails = []


def amp(anim, bone):
    """첫 프레임 대비 **최대 회전 각도**(도)와 Z 흔들림(cm).

    ## ⚠⚠ 로컬 변환은 회전만 변한다

    `get_bone_pose_for_time(..., use_source=False)` 는 **부모 기준(로컬)** 을 준다.
    본 길이는 안 변하므로 루트가 아닌 본의 **위치는 상수**다 — 위치 흔들림으로
    재면 전부 0 이 나와서 「레퍼런스 포즈」로 오독한다. 실제로 한 번 그랬다.

    ⚠ 오일러 한 축(pitch)만 봐도 안 된다. 축이 다른 회전이 통째로 안 보인다.
      쿼터니언 각거리로 **축과 무관하게** 잰다.
    """
    ln = anim.get_play_length()
    n = unreal.AnimationLibrary.get_num_frames(anim)
    if n <= 1 or ln <= 0:
        return None
    base = None
    max_ang = 0.0
    zlo = zhi = None
    for k in range(0, n, max(1, n // 30)):
        t = ln * (k / float(max(n - 1, 1)))
        try:
            tr = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, t, False)
        except Exception:
            return None
        q = tr.rotation
        if base is None:
            base = q
        else:
            d = abs(base.x * q.x + base.y * q.y + base.z * q.z + base.w * q.w)
            d = min(1.0, max(-1.0, d))
            max_ang = max(max_ang, 2.0 * math.degrees(math.acos(d)))
        z = tr.translation.z
        zlo = z if zlo is None else min(zlo, z)
        zhi = z if zhi is None else max(zhi, z)
    return (zhi - zlo, max_ang)


# Mixamo 본과 마네퀸 본의 짝 — **원본과 같은 자로 재야** 판정이 된다.
#
# ⚠⚠ 「각도가 작다」만으로는 못 고른다. 원본부터 잔잔한 애님(Idle 류)이 있기
#   때문이다. 원본 대비 **몇 %가 살아남았는지**를 봐야 한다.
L('V === 리타겟 품질 — 원본(Mixamo) 대비 ===')
L('V   %-22s %-28s %-28s %s' % ('애님', '마네퀸 최대회전각', 'Mixamo 최대회전각', '보존율'))
for name in CHECK:
    p_new = '%s/%s' % (OUT, name)
    p_old = '%s/%s' % (OLD, name)
    if not EAL.does_asset_exist(p_new):
        E('V   %-22s 없다' % name)
        fails.append(name)
        continue
    a = EAL.load_asset(p_new)
    b = EAL.load_asset(p_old) if EAL.does_asset_exist(p_old) else None
    sk = a.get_editor_property('skeleton')

    new_best = 0.0
    old_best = 0.0
    shown_n, shown_o = [], []
    for tb, sb in PAIRS:
        r = amp(a, tb)
        if r:
            new_best = max(new_best, r[1])
            shown_n.append('%s %.0f' % (tb.replace('_l', 'L'), r[1]))
        if b:
            r2 = amp(b, sb)
            if r2:
                old_best = max(old_best, r2[1])
                shown_o.append('%s %.0f' % (sb, r2[1]))
    ratio = (new_best / old_best) if old_best > 0.5 else 0.0
    ok = (old_best < 5.0) or (ratio >= 0.5)
    L('V   %-22s %-28s %-28s %.0f%%  %s'
      % (name, ' '.join(shown_n)[:27], ' '.join(shown_o)[:27], ratio * 100,
         'OK' if ok else '<-- 원본보다 많이 죽었다'))
    if str(sk.get_name()) != 'SK_Mannequin':
        E('V      스켈레톤이 %s 다' % sk.get_name())
        fails.append(name + ' 스켈레톤')
    if not ok:
        fails.append('%s 보존율 %.0f%%' % (name, ratio * 100))

L('V')
L('V === 골반 높이 — 마네퀸 비율로 왔나 ===')
for name in ('Idle', 'Walk_Fwd_Manny'):
    for base, label in ((OUT, '마네퀸'), (OLD, 'Mixamo')):
        p = '%s/%s' % (base, name)
        if not EAL.does_asset_exist(p):
            continue
        a = EAL.load_asset(p)
        bone = 'pelvis' if label == '마네퀸' else 'Hips'
        try:
            tr = unreal.AnimationLibrary.get_bone_pose_for_time(a, bone, 0.0, False)
            L('V   %-18s %-8s %-10s Z %.1f' % (name, label, bone, tr.translation.z))
        except Exception as ex:
            L('V   %-18s %-8s 못 읽음 %s' % (name, label, str(ex)[:40]))

L('V')
L('V === 몽타주 ===')
for m in ('AM_Loco_Walk', 'AM_Loco_Idle', 'AM_Loco_Sit', 'AM_Loco_CarryWalk',
          'AM_Loco_CarryIdle', 'AM_Reaching_Out', 'AM_Idle', 'AM_Eating_a_Burger'):
    p = '%s/%s' % (OUT, m)
    if not EAL.does_asset_exist(p):
        E('V   %-22s 없다' % m)
        fails.append(m)
        continue
    a = EAL.load_asset(p)
    sk = a.get_editor_property('skeleton')
    try:
        rm = a.get_editor_property('enable_root_motion')
    except Exception:
        rm = '?'
    L('V   %-22s %6.2fs 스켈레톤 %-14s 루트모션 %s'
      % (m, a.get_play_length(), sk.get_name() if sk else None, rm))
    if sk and str(sk.get_name()) != 'SK_Mannequin':
        fails.append(m + ' 스켈레톤')
    if rm is True:
        E('V      ⚠ 루트모션이 켜져 있다 — MaxWalkSpeed 를 덮어써서 손님이 안 걷는다 (§9-2)')
        fails.append(m + ' 루트모션')

L('V')
if fails:
    E('V === 실패 %d건: %s ===' % (len(fails), fails))
else:
    L('V === 전부 통과 ===')
