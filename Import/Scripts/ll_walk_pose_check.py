"""새 걷기 애님의 **자세**를 기존 `Walking` 과 견준다.

## 왜 필요한가

리타겟의 고전적 실패는 **A포즈/T포즈 정렬 어긋남**이다 — 팔이 45도 벌어진 채로
걷는다. 그런데 스켈레톤·길이·트랙 수·움직임량은 전부 정상으로 나온다. 그래서
`ll_walk_verify.py` 는 이걸 못 잡는다.

## 어떻게 보나 — **몸통 기준 팔·다리 방향**

`get_bone_pose_for_time` 은 **부모 기준 로컬** 트랜스폼을 준다. 팔이 어디를
가리키는지 보려면 `find_bone_path_to_root` 로 조상을 훑어 **컴포넌트 스페이스**로
합성해야 한다.

⚠ 예전에 「발 - 골반」을 로컬 좌표로 빼서 보폭을 쟀다가, 사실은 골반 속도를
  재고 있었다. 로컬을 컴포넌트로 착각하면 그럴듯한 숫자가 나와서 더 위험하다.

기준은 **같은 스켈레톤의 기존 `Walking`** 이다 (화면에서 멀쩡히 걷는 것이
확인된 애님). 팔이 몸통에서 벌어진 각도가 그것과 크게 다르면 정렬이 틀린 것이다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

NEW = '/Game/BurgerHouse/Anims/Mixamo/Anims/Walk_Fwd_Manny'
REF = '/Game/BurgerHouse/Anims/Mixamo/Anims/Walking'
N = 16          # 시간 표본


def comp_transform(seq, bone, t):
    """부모를 타고 올라가 **컴포넌트 스페이스** 트랜스폼을 만든다."""
    path = [str(b) for b in unreal.AnimationLibrary.find_bone_path_to_root(seq, bone)]
    # find_bone_path_to_root 는 [자기 ... 루트] 순서다. 루트부터 곱해 내려온다.
    out = unreal.Transform()
    for b in reversed(path):
        local = unreal.AnimationLibrary.get_bone_pose_for_time(seq, b, t, False)
        out = local.multiply(out) if hasattr(local, 'multiply') else out
    return out


def comp_location(seq, bone, t):
    path = [str(b) for b in unreal.AnimationLibrary.find_bone_path_to_root(seq, bone)]
    m = unreal.Matrix.IDENTITY if hasattr(unreal, 'Matrix') else None
    acc = unreal.Transform()
    first = True
    for b in reversed(path):
        local = unreal.AnimationLibrary.get_bone_pose_for_time(seq, b, t, False)
        if first:
            acc = local
            first = False
        else:
            # 자식 로컬 * 부모 누적 = 컴포넌트
            acc = unreal.MathLibrary.compose_transforms(local, acc)
    return acc.translation


def angle_between(a, b):
    dot = a.x * b.x + a.y * b.y + a.z * b.z
    la = (a.x ** 2 + a.y ** 2 + a.z ** 2) ** 0.5
    lb = (b.x ** 2 + b.y ** 2 + b.z ** 2) ** 0.5
    if la < 1e-6 or lb < 1e-6:
        return None
    c = max(-1.0, min(1.0, dot / (la * lb)))
    import math
    return math.degrees(math.acos(c))


def sub(a, b):
    return unreal.Vector(a.x - b.x, a.y - b.y, a.z - b.z)


# 재는 것 — (이름, 시작본, 끝본, 기준본쌍) : 몸통축 대비 팔·다리가 벌어진 각도
DOWN = unreal.Vector(0.0, 0.0, -1.0)
LIMBS = [
    ('왼팔  (LeftArm->LeftForeArm)',   'LeftArm',    'LeftForeArm'),
    ('오른팔(RightArm->RightForeArm)', 'RightArm',   'RightForeArm'),
    ('왼다리(LeftUpLeg->LeftLeg)',     'LeftUpLeg',  'LeftLeg'),
    ('오른다리(RightUpLeg->RightLeg)', 'RightUpLeg', 'RightLeg'),
]


def profile(path):
    a = EAL.load_asset(path)
    dur = a.get_editor_property('sequence_length')
    out = {}
    for label, b0, b1 in LIMBS:
        angs = []
        for i in range(N):
            t = min(dur * i / float(N - 1), dur - 1e-4)
            try:
                p0 = comp_location(a, b0, t)
                p1 = comp_location(a, b1, t)
            except Exception as ex:
                E('  %s 컴포넌트 스페이스 실패: %s' % (label, str(ex)[:90]))
                return None
            ang = angle_between(sub(p1, p0), DOWN)
            if ang is not None:
                angs.append(ang)
        if angs:
            out[label] = (sum(angs) / len(angs), min(angs), max(angs))
    return out


L('=== 사지가 아래(-Z)와 이루는 각도 — 컴포넌트 스페이스 ===')
L('  0도 = 완전히 아래로 늘어뜨림 · 90도 = 수평으로 뻗음(T포즈)')
L('')

ref = profile(REF)
new = profile(NEW)
if not (ref and new):
    E('프로파일 실패')
    raise SystemExit

bad = []
L('  %-34s %18s %18s %s' % ('', '기존 Walking', '새 Walk_Fwd_Manny', '차이'))
for label, _, _ in LIMBS:
    r, n = ref.get(label), new.get(label)
    if not (r and n):
        continue
    d = abs(n[0] - r[0])
    tag = 'OK' if d < 20 else '<-- ⚠ 정렬 의심'
    if d >= 20:
        bad.append('%s 평균이 %.0f도 다르다' % (label, d))
    L('  %-34s %6.1f (%5.1f~%5.1f) %6.1f (%5.1f~%5.1f)  %5.1f도 %s'
      % (label, r[0], r[1], r[2], n[0], n[1], n[2], d, tag))

L('')
if bad:
    E('=== 자세가 의심스럽다 ===')
    for b in bad:
        E('  - %s' % b)
    E('  RTG_MannyWalk 를 열어 Edit Pose 로 맞출 것 (§7 의 A포즈/T포즈).')
else:
    L('=== 자세도 기존 애님과 같은 범위 — 정렬 OK ===')
