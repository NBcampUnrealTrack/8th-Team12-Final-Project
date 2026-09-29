"""걷기 애님의 **골반 전진 속도**를 잰다. 아무것도 안 고친다.

## ⚠⚠ 이 스크립트는 보폭(stride)을 재지 **않는다**

원래는 「디딤발은 땅에 붙어 있으니 발을 골반 기준으로 보면 보행 속도가 나온다」
는 계산이었다. 그런데 **`get_bone_pose_for_time` 은 부모 기준 로컬 트랜스폼을
준다** — 컴포넌트 스페이스가 아니다. `leftfoot` 의 로컬 이동은 뼈 길이라
사실상 상수이므로, 「발 − 골반」은 결국 **골반 이동의 부호를 뒤집은 것**이다.

그래서 이 자가 뱉는 값은 보폭이 아니라 **골반 전진 속도**다.

⚠ 그런데도 기존 `Walking` 에서 91.14 가 나와 문서의 97.5 와 6.5% 밖에 차이가
  안 났다. **그럴듯한 숫자가 나오는 것이 이 착각의 위험한 점이다** — 골반
  속도를 재고 있었고, `Walking` 의 골반이 실제로 97.5cm/s 로 전진하고 있었다.
  (그 자체가 §9-1 의 발견으로 이어졌다: 옛 `Walking` 은 제자리가 아니었다.)

컴포넌트 스페이스가 필요하면 `find_bone_path_to_root` 로 조상을 훑어 합성한다 —
`ll_walk_pose_check.py` 가 그렇게 한다.

## 그래서 무엇에 쓰나

「이 애님이 제자리인가, 골반이 얼마나 전진하는가」를 빠르게 보는 데 쓴다.
제자리 애님이면 0 에 가깝게 나온다 (`Idle` 0.27).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

N = 120          # 표본 수


def tracks_of(seq):
    try:
        return [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(seq)]
    except Exception:
        return []


def find(tracks, *cands):
    """FName 비교는 대소문자를 안 가리지만 파이썬 문자열은 가린다."""
    low = {t.lower(): t for t in tracks}
    for c in cands:
        if c.lower() in low:
            return low[c.lower()]
    return None


def xyz(seq, bone, t):
    p = unreal.AnimationLibrary.get_bone_pose_for_time(seq, bone, t, False)
    return p.translation if p else None


def stride_speed(seq, dur, hips, feet):
    """디딤 구간의 상대 속도 중앙값 (cm/s)."""
    speeds = []
    for foot in feet:
        prev = None
        for i in range(N):
            t = dur * i / float(N - 1)
            t = min(t, dur - 1e-4)
            h, f = xyz(seq, hips, t), xyz(seq, foot, t)
            if h is None or f is None:
                return None, []
            rel = (f.x - h.x, f.y - h.y)
            if prev is not None:
                dt = dur / float(N - 1)
                d = ((rel[0] - prev[0]) ** 2 + (rel[1] - prev[1]) ** 2) ** 0.5
                speeds.append(d / dt)
            prev = rel
    if not speeds:
        return None, []
    speeds.sort()
    # 하위 절반 = 디딤 구간. 그 중앙값을 쓴다.
    half = speeds[:max(1, len(speeds) // 2)]
    return half[len(half) // 2], speeds


def report(label, path, expect=None):
    if not EAL.does_asset_exist(path):
        E('  %-14s %s <-- 없다' % (label, path))
        return None
    a = EAL.load_asset(path)
    dur = a.get_editor_property('sequence_length')
    tr = tracks_of(a)
    hips = find(tr, 'hips', 'pelvis', 'mixamorig:Hips')
    lf = find(tr, 'leftfoot', 'foot_l', 'lefttoebase', 'ball_l')
    rf = find(tr, 'rightfoot', 'foot_r', 'righttoebase', 'ball_r')
    if not (hips and lf and rf):
        E('  %-14s 본을 못 찾음 hips=%s lf=%s rf=%s' % (label, hips, lf, rf))
        E('      트랙: %s' % tr[:20])
        return None

    # 골반 자체가 전진하는가 (제자리 애님이면 0 이어야 한다)
    h0, h1 = xyz(a, hips, 0.0), xyz(a, hips, dur - 1e-4)
    drift = ((h1.x - h0.x) ** 2 + (h1.y - h0.y) ** 2) ** 0.5

    sp, raw = stride_speed(a, dur, hips, (lf, rf))
    L('  %-14s %-26s %.3fs' % (label, path.split('/')[-1], dur))
    L('      골반: 전진 %.2fcm · Z %.1f · yaw %+.1f' % (drift, h0.z, h0.rotation.rotator().yaw if hasattr(h0,'rotation') else 0.0))
    L('      보폭 속도  %7.2f cm/s   (표본 %d · 최소 %.1f · 최대 %.1f)'
      % (sp or -1, len(raw), raw[0] if raw else -1, raw[-1] if raw else -1))
    if expect:
        err = abs((sp or 0) - expect) / expect * 100.0
        tag = 'OK' if err < 15 else '⚠ 자가 안 맞는다'
        L('      기대 %.1f cm/s 대비 오차 %.1f%%  <-- %s' % (expect, err, tag))
    L('')
    return sp


L('=== 자 검증: 기존 Walking 은 문서 실측 97.5 cm/s ===')
report('현재 손님', '/Game/BurgerHouse/Anims/Mixamo/Anims/Walking', expect=97.5)

L('=== 참고 ===')
report('Texting걷기', '/Game/BurgerHouse/Anims/Mixamo/Anims/Walking_While_Texting')
report('Idle(정지)', '/Game/BurgerHouse/Anims/Mixamo/Anims/Idle')
report('소스(마네퀸)', '/Game/Characters/Mannequins/Anims/Unarmed/RetargetWalk/A_WalkFwd_Loop')
L('⚠ 소스는 root 본에 전진이 실려 있다(168.18 cm/s). 골반 기준 보폭이 그와')
L('  비슷하게 나오면 자가 제대로 도는 것이다.')
