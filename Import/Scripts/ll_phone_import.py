"""`Talking_On_A_Cell_Phone` 에 쓸 휴대폰 **한 대**를 들여온다.

    에디터를 **끄고** 돌린다.

원본: `Downloads/phone/I+phone+17+pro.fbx` · `Downloads/phone/textures/*`

## ⚠⚠ 이 FBX 는 폰 한 대가 아니라 **쇼케이스 장면**이다

합치지 않고 구우면 스태틱메시가 **30개** 나오고, 전부 겹친 범위가
**729 × 36 × 404 cm** 다. 실측 —

```
Cylinder_024  중심 x   22.33   196.65 × 31.46 × 404.06   <- **폰 A 본체**
Cylinder_038  중심 x −233.23   같은 크기                  <- 폰 B 본체
Cylinder_045  중심 x  299.01   같은 크기                  <- 폰 C 본체
Cylinder_022… 44.95 원반                                 <- 카메라 렌즈
Cube_003…     16.32 사각판                                <- 센서 커버
Torx_Flat_Top 3.98                                        <- 나사
```

색상 변형 **세 대**(`textures` 의 Blue/Orange/Silver 와 짝이 맞는다)가 한 장면에
나란히 있다. 한 대에 부품 10개씩, 정확히 30개다.

⚠⚠ **크기가 26배다.** 본체가 404cm — 실물 15cm 의 27배다. 붙일 때 스케일
  **0.037** 이 출발점이다.

⚠ 「본체」를 얇은 사각판(`Cube_003`)으로 착각하기 쉽다. 16.32 × 1.08 × 16.32 는
  **정사각형**이라 폰이 아니다 — 처음에 그걸 골라 부품이 1개만 잡혔다.

⚠⚠ 그래서 `combine_static_meshes_behavior = ALL` 로 합치면 **세 대와 연출 부품이
  한 덩어리**가 된다. 「합치기가 안 먹는다」가 아니라 **합치면 안 되는 것**이었다.

⚠ `combine_static_meshes` 불리언은 **deprecated** 다 — behavior 만 본다. 그쪽을
  건드려 봐야 소용없다 (경고만 뜬다).

## 그래서 — 한 대를 골라 **본체만** 쓴다

정점이 월드 좌표로 구워져 있어서 부품 중심으로 폰 A 를 가려낼 수 있다 (본체
바운즈를 `MARGIN` 만큼 부풀린 상자와 겹치는 것 = 같은 폰, 실측 10개).

⚠⚠ 그 10개를 한 메시로 **머지하지는 못했다.** 커맨드릿에서 —

```
EditorLevelLibrary.merge_static_mesh_actors   deprecated · 조용히 None
get_editor_subsystem(StaticMeshEditorSubsystem)  -> None   (모듈이 안 올라온다)
```

그래서 **본체(`Cylinder_024`) 하나만** `Prop_Phone` 으로 쓴다. 손에 들고 귀에
대는 크기(15cm)에서 카메라 원반·나사는 1mm 급이라 거의 안 보인다.

⚠ 카메라 범프까지 원하면 **에디터에서** 부품 10개를 레벨에 놓고
  `Actor > Merge Actors` 로 구우면 된다. 고를 부품 이름은 아래 로그가 찍는다.

## ⚠ `.webp` 는 언리얼이 못 읽는다

`textures/` 의 색상 변형이 webp 다. 임포터가 **조용히 건너뛴다.** 읽을 수 있는
것만 들여오고 못 읽은 것은 이름을 찍는다.
"""
import unreal
import os

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

SRC_DIR = 'C:/Users/jesse/Downloads/phone'
FBX = os.path.join(SRC_DIR, 'I+phone+17+pro.fbx')
TEX_SRC = os.path.join(SRC_DIR, 'textures')
DEST = '/Game/BurgerHouse/Props/Phone'
TMP = '/Game/BurgerHouse/Props/_PhoneSplit'
TEXDIR = DEST + '/Textures'
NAME = 'Prop_Phone'

# 본체 바운즈를 이만큼 부풀려 「같은 폰의 부품」을 고른다.
# ⚠ 폰 사이 간격이 약 255cm 라 여유가 크다. 10 이면 옆 폰이 안 딸려 온다.
MARGIN = 10.0

OK_EXT = ('.png', '.jpg', '.jpeg', '.tga', '.bmp', '.exr', '.psd', '.hdr')
fails = []


def purge(folder):
    n = 0
    for a in EAL.list_assets(folder, recursive=True, include_folder=False):
        EAL.delete_asset(a.split('.')[0])
        n += 1
    if n:
        EAL.delete_directory(folder)
    return n


# ───────────────────────── 앞선 시도 치우기
L('P === 정리 ===')
L('P   %-40s %d개' % (DEST, purge(DEST)))
L('P   %-40s %d개' % (TMP, purge(TMP)))
# ⚠ 첫 시도가 `/Props` 루트에 부품을 쏟아 놓았다
STRAY = ('Cube_', 'Cylinder_', 'Plane_', 'Circle_', 'Sphere_', 'Torus_', 'Cam_',
         'Display', 'Prop_Phone', 'I+phone', 'Side_', 'Matte_', 'Metal_Mesh',
         'Mic', 'Torch', 'LIDAR', 'Dynamic_')
n = 0
for a in EAL.list_assets('/Game/BurgerHouse/Props', recursive=False, include_folder=False):
    nm = a.split('/')[-1].split('.')[0]
    if any(nm.startswith(k) for k in STRAY):
        EAL.delete_asset(a.split('.')[0])
        n += 1
L('P   /Props 루트 잔해 %d개' % n)

# ───────────────────────── 부품으로 굽는다
L('P')
L('P === 부품으로 굽기 (합치지 않는다) ===')
p = unreal.InterchangeGenericAssetsPipeline()
mp = p.get_editor_property('mesh_pipeline')
mp.set_editor_property('import_static_meshes', True)
mp.set_editor_property('combine_static_meshes_behavior',
                       unreal.InterchangeCombineStaticMeshesBehavior.DO_NOT_COMBINE)
mp.set_editor_property('collision', False)
mp.set_editor_property('force_collision_primitive_generation', False)

t = unreal.AssetImportTask()
t.set_editor_property('filename', FBX)
t.set_editor_property('destination_path', TMP)
t.set_editor_property('automated', True)
t.set_editor_property('replace_existing', True)
t.set_editor_property('save', True)
t.set_editor_property('options', p)
TOOLS.import_asset_tasks([t])

parts = [a for a in EAL.list_assets(TMP, recursive=True, include_folder=False)
         if isinstance(EAL.load_asset(a), unreal.StaticMesh)]
L('P   부품 %d개' % len(parts))

# ───────────────────────── 폰 한 대 고르기
L('P')
L('P === 폰 한 대 고르기 ===')
# ⚠ 본체 = **부피가 가장 큰** 부품. 세 대가 같은 크기라, 그중 **원점에 가장
#   가까운** 것을 골라 재현 가능하게 한다.
def vol(b):
    return b.box_extent.x * b.box_extent.y * b.box_extent.z


cand = []
for a in parts:
    b = EAL.load_asset(a).get_bounds()
    cand.append((vol(b), a, b))
cand.sort(key=lambda x: -x[0])
big_v = cand[0][0]
bodies = [(a, b) for v, a, b in cand if v >= big_v * 0.9]
L('P   본체 후보 %d개: %s'
  % (len(bodies), [x[0].split('/')[-1].split('.')[0] for x in bodies]))
bodies.sort(key=lambda x: abs(x[1].origin.x) + abs(x[1].origin.y) + abs(x[1].origin.z))
body_path, body_b = bodies[0]
L('P   -> %s  중심(%.2f %.2f %.2f) 크기 %.2f x %.2f x %.2f'
  % (body_path.split('/')[-1].split('.')[0], body_b.origin.x, body_b.origin.y, body_b.origin.z,
     body_b.box_extent.x * 2, body_b.box_extent.y * 2, body_b.box_extent.z * 2))


def aabb(b, pad=0.0):
    return ((b.origin.x - b.box_extent.x - pad, b.origin.x + b.box_extent.x + pad),
            (b.origin.y - b.box_extent.y - pad, b.origin.y + b.box_extent.y + pad),
            (b.origin.z - b.box_extent.z - pad, b.origin.z + b.box_extent.z + pad))


# ⚠ 「원점이 상자 안」이 아니라 **바운즈가 겹치는지**로 고른다. 카메라 범프처럼
#   본체 밖으로 튀어나온 부품은 원점이 밖에 있을 수 있다.
BOX = aabb(body_b, MARGIN)
mine = []
for a in parts:
    q = aabb(EAL.load_asset(a).get_bounds())
    if all(q[i][0] <= BOX[i][1] and BOX[i][0] <= q[i][1] for i in range(3)):
        mine.append(a)
L('P   같은 폰의 부품 %d개 / 전체 %d개 — %s'
  % (len(mine), len(parts), ', '.join(x.split('/')[-1].split('.')[0] for x in mine)))
if len(mine) < 2:
    E('P   부품이 너무 적다 — MARGIN 을 볼 것')
    fails.append('부품 선별')

# ───────────────────────── 본체만 꺼낸다
L('P')
L('P === 본체를 Prop_Phone 으로 ===')
mesh_path = '%s/%s' % (DEST, NAME)
if EAL.does_asset_exist(mesh_path):
    EAL.delete_asset(mesh_path)
L('P   rename %s -> %s  %s'
  % (body_path.split('/')[-1].split('.')[0], NAME,
     EAL.rename_asset(body_path.split('.')[0], mesh_path)))

# ⚠⚠ 머티리얼도 **같이 옮긴다.** 임시 폴더째 지우면 슬롯이 비어 회색이 된다 —
#    실제로 한 번 그랬다.
m = EAL.load_asset(mesh_path)
moved = 0
for sm in m.get_editor_property('static_materials'):
    mi = sm.get_editor_property('material_interface')
    if not mi or TMP not in mi.get_path_name():
        continue
    dst = '%s/%s' % (DEST, mi.get_name())
    if not EAL.does_asset_exist(dst):
        if EAL.rename_asset(mi.get_path_name().split('.')[0], dst):
            moved += 1
L('P   머티리얼 %d개 옮김' % moved)
EAL.save_asset(mesh_path, only_if_is_dirty=False)

# ───────────────────────── 텍스처 (읽을 수 있는 것만)
L('P')
L('P === 텍스처 ===')
tasks, skipped = [], []
for fn in sorted(os.listdir(TEX_SRC)):
    if os.path.splitext(fn)[1].lower() not in OK_EXT:
        skipped.append(fn)
        continue
    tt = unreal.AssetImportTask()
    tt.set_editor_property('filename', os.path.join(TEX_SRC, fn))
    tt.set_editor_property('destination_path', TEXDIR)
    tt.set_editor_property('automated', True)
    tt.set_editor_property('replace_existing', True)
    tt.set_editor_property('save', True)
    tasks.append(tt)
if tasks:
    TOOLS.import_asset_tasks(tasks)
for a in EAL.list_assets(TEXDIR, recursive=True, include_folder=False):
    L('P   들어옴 %s' % a.split('/')[-1].split('.')[0])
if skipped:
    L('P   ⚠ 언리얼이 못 읽어 건너뜀: %s' % ', '.join(skipped))

# ───────────────────────── 실측
L('P')
L('P === 실측 ===')
if EAL.does_asset_exist(mesh_path):
    m = EAL.load_asset(mesh_path)
    b = m.get_bounds()
    L('P   %-12s 크기 %.2f x %.2f x %.2f cm · 피벗에서 중심까지 (%.2f %.2f %.2f) · 면 %d'
      % (NAME, b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2,
         b.origin.x, b.origin.y, b.origin.z, m.get_num_triangles(0)))
    for i, sm in enumerate(m.get_editor_property('static_materials')):
        mi = sm.get_editor_property('material_interface')
        L('P   슬롯%d %-24s %s' % (i, sm.get_editor_property('material_slot_name'),
                                  mi.get_path_name().split('.')[0] if mi else '(없음)'))
        # ⚠ 임시 폴더를 가리키면 아래 정리에서 사라진다
        if mi and TMP in mi.get_path_name():
            fails.append('슬롯%d 가 임시 폴더를 가리킨다' % i)
    big = max(b.box_extent.x, b.box_extent.y, b.box_extent.z) * 2
    L('P   가장 긴 변 %.2fcm -> 실물(약 15cm) 기준 스케일 %.3f' % (big, 15.0 / big if big else 0))
    EAL.save_asset(mesh_path, only_if_is_dirty=False)

L('P')
if fails:
    E('P === 확인 필요: %s ===' % fails)
    L('P   ⚠ 임시 폴더는 **안 지웠다** — 머티리얼 참조가 거기 살아 있다')
else:
    L('P   임시 폴더 정리 %d개' % purge(TMP))
    L('P === 완료 ===')
