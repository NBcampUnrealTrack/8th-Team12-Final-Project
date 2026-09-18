"""`Carrying` 애님의 **양손 사이**에 트레이가 오도록 `TrayHandOffset` 을 잰다.

    에디터를 **끄고** 돌린다.

## ⚠⚠ `get_bone_pose_for_time` 은 **부모 기준(로컬)** 이다

컴포넌트 공간이 아니다. 손 위치를 알려면 루트까지의 본 사슬을 타고 **직접
합성**해야 한다. 예전에 이걸 모르고 「보폭 측정기」를 만들었다가 골반 속도를
재고 있었다 (§7).

## 어떻게 정하나

```
Pl, Pr = 컴포넌트 공간의 hand_l · hand_r 위치
중점    = (Pl + Pr) / 2
가로축  = normalize(Pl − Pr)      -> 트레이의 긴 축(+Y, 40cm)
위      = (0,0,1)
앞      = 가로축 × 위             -> 트레이 +X
```

이 회전·위치를 **붙는 본의 로컬 공간**으로 되돌린 것이 `TrayHandOffset` 이다.

⚠ 트레이 메시는 원점이 **바닥**이다 (`Props/Tray` 는 z 0~2.2). 그래서 중점에
  그대로 놓으면 손이 트레이 **바닥**을 잡는 그림이 된다. 손이 테두리를 잡도록
  살짝 내린다.

⚠ 애님 내내 손이 벌어졌다 좁아지면 한 프레임으로 정하면 안 된다. 여러 프레임을
  재서 **평균**과 **흔들림**을 같이 찍는다 — 흔들림이 크면 이 방식 자체가 안 맞는다.
"""
import unreal
import math

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

ANIM = '/Game/BurgerHouse/Anims/Manny/Carrying'
BP = '/Game/BurgerHouse/BP/BP_Customer'
MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'

# 어디에 붙일까 — 몸통이 손보다 안정적이다 (손은 애님마다 움직인다)
ATTACH_BONE = 'spine_05'

# 손이 테두리를 잡도록 내리는 양(cm). 트레이 두께가 2.2 라 그 절반쯤.
DROP = 4.0

anim = EAL.load_asset(ANIM)
mesh = EAL.load_asset(MESH)
if not anim or not mesh:
    E('H  애님(%s) 또는 메시(%s) 가 없다' % (ANIM, MESH))
    raise SystemExit

AL = unreal.AnimationLibrary


def chain_to_root(bone):
    """본에서 루트까지의 이름 목록 (자기 자신 포함, 루트가 마지막).

    ⚠ `get_bone_parent` 는 이 판본에 없다. `find_bone_path_to_root` 를 쓴다 —
      **루트 -> 본** 순서로 준다.
    """
    path = AL.find_bone_path_to_root(anim, bone)
    return [str(b) for b in reversed(list(path))]


def comp_transform(bone, t):
    """컴포넌트 공간 트랜스폼 — 로컬을 루트까지 **합성**한다.

    ⚠⚠ `get_bone_pose_for_time` 은 **부모 기준**이다. 합성을 빼먹으면 손 위치가
       아니라 「팔뚝에서 손까지의 거리」를 읽는다 — 실제로 (27.3, 0, 0) 이 나왔다.
    """
    acc = unreal.Transform()
    # ⚠⚠ **루트부터** 내려가야 한다. `chain_to_root` 가 이미 루트 우선이라
    #    여기서 또 뒤집으면 리프부터 합성해 값이 통째로 틀린다 — 실측으로
    #    손이 골반보다 **108cm 아래**, 양손 간격 199cm 가 나왔다.
    for b in chain_to_root(bone):                    # 루트 -> 본
        local = AL.get_bone_pose_for_time(anim, b, t, False)
        acc = unreal.MathLibrary.compose_transforms(local, acc)
    return acc


L('H === Carrying 의 손 위치 (컴포넌트 공간) ===')
ln = anim.get_play_length()
n = AL.get_num_frames(anim)
L('H   길이 %.2fs · %d프레임' % (ln, n))
L('H   %s 사슬: %s' % ('hand_l', ' <- '.join(chain_to_root('hand_l'))))

samples = []
for k in range(0, 9):
    t = ln * (k / 8.0)
    tl = comp_transform('hand_l', t)
    tr = comp_transform('hand_r', t)
    ta = comp_transform(ATTACH_BONE, t)
    pl, pr = tl.translation, tr.translation
    mid = unreal.Vector((pl.x + pr.x) / 2, (pl.y + pr.y) / 2, (pl.z + pr.z) / 2)
    d = math.sqrt((pl.x - pr.x) ** 2 + (pl.y - pr.y) ** 2 + (pl.z - pr.z) ** 2)
    samples.append((t, pl, pr, mid, d, ta))
    L('H   t=%4.2f  L(%6.1f %6.1f %6.1f)  R(%6.1f %6.1f %6.1f)  중점(%6.1f %6.1f %6.1f)  간격 %.1f'
      % (t, pl.x, pl.y, pl.z, pr.x, pr.y, pr.z, mid.x, mid.y, mid.z, d))

# 흔들림
mx = [s[3].x for s in samples]
my = [s[3].y for s in samples]
mz = [s[3].z for s in samples]
ds = [s[4] for s in samples]
L('H   중점 흔들림  X %.1f · Y %.1f · Z %.1f   간격 %.1f~%.1f'
  % (max(mx) - min(mx), max(my) - min(my), max(mz) - min(mz), min(ds), max(ds)))
if max(mx) - min(mx) > 15 or max(mz) - min(mz) > 15:
    E('H   ⚠ 손 중점이 애님 내내 크게 움직인다 — 고정 오프셋으로는 안 맞는다')

# ───────────────────────── 평균으로 오프셋을 푼다
mid = unreal.Vector(sum(mx) / len(mx), sum(my) / len(my), sum(mz) / len(mz))
# 방향은 중간 프레임 기준 (양손을 잇는 선)
_, pl, pr, _, _, ta = samples[len(samples) // 2]
# ⚠⚠ **오른손 -> 왼손이 아니라 왼손 -> 오른손**이다. UE 는 왼손 좌표계라
#    (X 앞 · Y 오른쪽 · Z 위) 축 셋이 그 순서로 맞아야 회전이 **거울상**이 안 된다.
#    반대로 잡으면 축 둘이 뒤바뀌어 행렬식이 음수가 되고, 트레이 위 음식 배치가
#    좌우로 뒤집힌다.
right = unreal.Vector(pr.x - pl.x, pr.y - pl.y, pr.z - pl.z)
rl = math.sqrt(right.x ** 2 + right.y ** 2 + right.z ** 2)
right = unreal.Vector(right.x / rl, right.y / rl, right.z / rl)
up = unreal.Vector(0.0, 0.0, 1.0)
# 앞 = 오른쪽 × 위  (X = Y × Z 가 맞는 순서다)
fwd = unreal.Vector(right.y * up.z - right.z * up.y,
                    right.z * up.x - right.x * up.z,
                    right.x * up.y - right.y * up.x)
fl = math.sqrt(fwd.x ** 2 + fwd.y ** 2 + fwd.z ** 2)
fwd = unreal.Vector(fwd.x / fl, fwd.y / fl, fwd.z / fl)

# ⚠ 트레이 긴 축(+Y, 40)이 **양손을 잇는 선**을 따라야 한다. +Y 가 왼손 쪽이다.
rot = unreal.MathLibrary.make_rotation_from_axes(fwd, right, up)
# ⚠ `unreal.Transform(location, rotation, scale)` 순서다. 뒤집으면 TypeError.
want = unreal.Transform(unreal.Vector(mid.x, mid.y, mid.z - DROP), rot,
                        unreal.Vector(1, 1, 1))

L('H')
L('H === 결과 ===')
L('H   붙는 본        %s' % ATTACH_BONE)
L('H   본 컴포넌트 위치 (%.1f %.1f %.1f)' % (ta.translation.x, ta.translation.y, ta.translation.z))
L('H   손 중점(평균)   (%.1f %.1f %.1f)  · 손 내림 %.1f' % (mid.x, mid.y, mid.z, DROP))

# 본 로컬로 되돌린다
offset = unreal.MathLibrary.compose_transforms(want, ta.inverse()) if False else \
    unreal.MathLibrary.make_relative_transform(want, ta)
L('H   TrayHandOffset  loc(%.2f %.2f %.2f)  rot(%.1f %.1f %.1f)'
  % (offset.translation.x, offset.translation.y, offset.translation.z,
     offset.rotation.rotator().roll, offset.rotation.rotator().pitch,
     offset.rotation.rotator().yaw))

# ───────────────────────── BP 에 꽂는다
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
before_s = cdo.get_editor_property('tray_socket')
before_o = cdo.get_editor_property('tray_hand_offset')
cdo.set_editor_property('tray_socket', ATTACH_BONE)
cdo.set_editor_property('tray_hand_offset', offset)
L('H   TraySocket      %s -> %s' % (before_s, ATTACH_BONE))
L('H   예전 오프셋      loc %s' % before_o.translation)
L('H   save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ───────────────────────── 되짚기 — 정말 손 사이로 가나
L('H')
L('H === 되짚기: 오프셋을 다시 컴포넌트 공간으로 ===')
for t, pl, pr, m, d, ta2 in samples[::2]:
    got = unreal.MathLibrary.compose_transforms(offset, ta2)
    g = got.translation
    err = math.sqrt((g.x - m.x) ** 2 + (g.y - m.y) ** 2 + (g.z - (m.z - DROP)) ** 2)
    L('H   t=%4.2f  트레이(%6.1f %6.1f %6.1f)  손중점(%6.1f %6.1f %6.1f)  오차 %.1fcm'
      % (t, g.x, g.y, g.z, m.x, m.y, m.z, err))
