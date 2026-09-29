"""Album — 앞면을 정사각형으로 만들고, 커버를 반대 면(-Y)으로 옮긴다.

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_album_adjust.py"

## 1. 정사각형

커버는 600×600 정사각형인데 앞면은 18.8 × 21.1 (비율 0.889) 이라 세로로 11%
늘어났다. X 스케일을 |Z| 에 맞춰 **0.188 → 0.211** 로 올린다. 두께(Y) 와 Z 는
그대로다. 부호도 그대로 둔다 — Z 의 음수 부호는 배치의 일부다.

## 2. 반대 면

`FrontSign = -1` 이면 마스크가 `LocalPosition.y < -25` 로 뒤집혀 커버가 -Y 면에
간다.

⚠⚠ **면을 바꾸면 `FlipU` 도 같이 바뀌어야 한다.** 앞면 법선 N 이 반대가 되니
거울상 조건 `(U × V)·N > 0` 의 부호가 뒤집힌다. 여기서는 계산으로 다시 구한다
— 손으로 토글하지 않는다.

상하 조건 `V·월드up > 0` 은 면과 무관하다 (V 는 로컬 +Z 의 월드 방향이라
어느 면을 보든 같다). 그래서 `FlipV` 는 바뀌지 않는다.
"""
import unreal

L = unreal.log_warning
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

MI = '/Game/BurgerHouse/Materials/MI_AlbumCover'
LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
FRONT_SIGN = -1.0          # 커버를 놓을 면: +1 = 로컬 +Y, -1 = 로컬 -Y

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(LEVEL)

album = None
for a in eas.get_all_level_actors():
    if a.get_actor_label() == 'Album':
        album = a
        break
if not album:
    raise RuntimeError('Album 액터가 없다')

# ── 1. 앞면을 정사각형으로 ──────────────────────────────────────────
L('=== 1. 정사각형 ===')
old = album.get_actor_transform().scale3d
L('  전 = (%.3f, %.3f, %.3f)  앞면 %.1f x %.1f (비율 %.3f)'
  % (old.x, old.y, old.z, abs(old.x) * 100, abs(old.z) * 100, abs(old.x / old.z)))

new_x = abs(old.z) * (1.0 if old.x >= 0 else -1.0)      # 크기는 |Z| 에 맞추고 부호는 유지
album.set_actor_scale3d(unreal.Vector(new_x, old.y, old.z))

scl = album.get_actor_transform().scale3d
L('  후 = (%.3f, %.3f, %.3f)  앞면 %.1f x %.1f (비율 %.3f)'
  % (scl.x, scl.y, scl.z, abs(scl.x) * 100, abs(scl.z) * 100, abs(scl.x / scl.z)))

# ── 2. 방향 재계산 ──────────────────────────────────────────────────
L('')
L('=== 2. 방향 재계산 (FrontSign = %+.0f) ===' % FRONT_SIGN)
q = album.get_actor_transform().rotation


def sign(x):
    return 1.0 if x >= 0 else -1.0


def rot(vx, vy, vz):
    v = q.rotate_vector(unreal.Vector(vx, vy, vz))
    return (v.x, v.y, v.z)


def mul(v, s):
    return (v[0] * s, v[1] * s, v[2] * s)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


U = mul(rot(1, 0, 0), sign(scl.x))          # 이미지의 오른쪽
V = mul(rot(0, 0, 1), sign(scl.z))          # 이미지의 위
N = mul(rot(0, 1, 0), FRONT_SIGN)           # 커버가 붙는 면의 법선

L('  U(이미지 오른쪽) = (%.3f, %.3f, %.3f)' % U)
L('  V(이미지 위)     = (%.3f, %.3f, %.3f)' % V)
L('  N(커버 면 법선)  = (%.3f, %.3f, %.3f)' % N)

flip_v = 1.0 if V[2] < 0 else 0.0
handed = dot(cross(U, V), N)
if flip_v:
    handed = -handed
# ⚠⚠ **UE 는 왼손 좌표계다.** 카메라의 right 는 `up × forward` 다
#   (단위 로테이터로 확인: (0,0,1) x (1,0,0) = (0,1,0) = +Y = right).
#   면을 바깥(+N)에서 보면 forward = -N, up = V 이므로
#       right = V x (-N) = -(V x N)
#   이미지의 오른쪽 U 가 이것과 같아야 하니 U·(V x N) < 0,
#   즉 **(U x V)·N < 0 이어야 거울상이 아니다** (스칼라 삼중적은 순환 불변).
#   오른손 좌표계 감각으로 `> 0` 이라고 썼다가 좌우가 뒤집혔다 (2026-09-18).
flip_u = 1.0 if handed > 0 else 0.0

L('  -> FlipV = %d  (V.z = %.3f · 면과 무관)' % (flip_v, V[2]))
L('  -> FlipU = %d  ((U x V)·N = %+.3f · 음수여야 거울상이 아니다)' % (flip_u, handed))

# 이 면이 손님 쪽을 보는지 확인 (판단 근거를 남긴다)
loc = album.get_actor_location()
for o in eas.get_all_level_actors():
    if o.get_actor_label() == 'LL_Light_PlayerStart':
        p = o.get_actor_location()
        d = (p.x - loc.x, p.y - loc.y, p.z - loc.z)
        n = (d[0] ** 2 + d[1] ** 2 + d[2] ** 2) ** 0.5
        L('  PlayerStart 방향과의 내적 = %+.3f  (양수면 그쪽을 본다)'
          % dot(N, (d[0] / n, d[1] / n, d[2] / n)))

# ── 3. 인스턴스에 반영 ──────────────────────────────────────────────
L('')
L('=== 3. MI_AlbumCover ===')
mi = EAL.load_asset(MI)
MEL.set_material_instance_scalar_parameter_value(mi, 'FrontSign', FRONT_SIGN)
MEL.set_material_instance_scalar_parameter_value(mi, 'FlipU', flip_u)
MEL.set_material_instance_scalar_parameter_value(mi, 'FlipV', flip_v)
MEL.update_material_instance(mi)
EAL.save_asset(MI, only_if_is_dirty=False)
for n in ('FrontSign', 'FlipU', 'FlipV'):
    L('  %-10s = %+.0f' % (n, MEL.get_material_instance_scalar_parameter_value(mi, n)))

les.save_current_level()
L('')
L('레벨 저장 · 검산은 새 프로세스에서')
