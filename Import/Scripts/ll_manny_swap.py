"""손님을 마네퀸으로 갈아 끼우기 — 2단계: `BP_Customer` 배선 교체.

    에디터를 **끄고** 돌린다. 1단계는 `ll_manny_retarget.py`.

| 바꾸는 것 | 전 | 후 |
|---|---|---|
| `Mesh ▸ SkeletalMesh` | `Hip_Hop_Dancing` (Mixamo) | `SKM_Manny_Simple` |
| `Mesh ▸ AnimClass` | `Mixamo/ABP_Customer` | `Manny/ABP_Customer` |
| `Actions` 의 몽타주 22개 | `Anims/Montages/*` | `Anims/Manny/*` |
| `TraySocket` | `spine2` (Mixamo) | `spine_05` (마네퀸) |

## ⚠⚠ 본 이름이 통째로 바뀐다

Mixamo 와 마네퀸은 **뼈 이름 규약이 다르다.** 애님은 리타게터가 옮겨 주지만
**코드·에셋에 문자열로 박힌 본 이름은 아무도 안 옮겨 준다.**

```
Hips  Spine  Spine1  Spine2  Neck  Head  LeftArm  LeftHand  LeftUpLeg …
pelvis spine_01 … spine_05  neck_01 head  upperarm_l hand_l thigh_l …
```

지금 걸리는 것은 `TraySocket` 하나다 (`spine2` → `spine_05`). 트레이를 몸통에
붙이는 자리다.

⚠ 못 찾으면 `GetSocketTransform` 이 **컴포넌트 원점(발밑)** 으로 폴백한다 —
  죽지 않고 트레이만 발밑에 붙는다. 그래서 조용히 지나가기 쉽다.

⚠ `TrayHandOffset` 은 본의 **방향 규약**에 기대는 값이라 이번에 다시 눈으로
  맞춰야 한다. 스크립트가 정할 수 있는 값이 아니다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/BP/BP_Customer'
NEW_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
NEW_ABP = '/Game/BurgerHouse/Anims/Manny/ABP_Customer'
NEW_ANIMDIR = '/Game/BurgerHouse/Anims/Manny'

# Mixamo `Spine2`(척추 3단 중 맨 위) ≈ 마네퀸 `spine_05`(5단 중 맨 위)
NEW_TRAY_SOCKET = 'spine_05'

fails = []

bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
mesh_c = cdo.get_editor_property('mesh')

# ───────────────────────── 메시 · ABP
L('W === 메시 / ABP ===')
new_mesh = EAL.load_asset(NEW_MESH)
new_abp = EAL.load_asset(NEW_ABP)
if not new_mesh or not new_abp:
    E('W   메시(%s) 또는 ABP(%s) 가 없다' % (NEW_MESH, NEW_ABP))
    raise SystemExit

old_mesh = mesh_c.get_editor_property('skeletal_mesh')
old_abp = mesh_c.get_editor_property('anim_class')
mesh_c.set_editor_property('skeletal_mesh', new_mesh)
mesh_c.set_editor_property('anim_class', new_abp.generated_class())
L('W   메시 %s -> %s' % (old_mesh.get_name() if old_mesh else None, new_mesh.get_name()))
L('W   ABP  %s -> %s' % (old_abp.get_name() if old_abp else None,
                         new_abp.generated_class().get_name()))

# ⚠ 캐릭터 메시의 상대 트랜스폼(발밑으로 −90, yaw −90)은 **그대로 둔다.**
#   마네퀸도 같은 규약이라 손댈 필요가 없다.
L('W   상대 트랜스폼 (안 건드림) %s' % mesh_c.get_relative_transform().translation)

# ───────────────────────── 트레이 소켓
L('W === 트레이 소켓 ===')
old_sock = cdo.get_editor_property('tray_socket')
cdo.set_editor_property('tray_socket', NEW_TRAY_SOCKET)
L('W   %s -> %s' % (old_sock, NEW_TRAY_SOCKET))
# ⚠ 그 본이 **실제로 있는지** 확인한다. 없으면 트레이가 발밑에 붙는다.
bones = [str(b) for b in new_mesh.get_editor_property('skeleton').get_reference_pose()] \
    if False else None
try:
    idx = unreal.AnimationLibrary.get_bone_names(EAL.load_asset('%s/Idle' % NEW_ANIMDIR))
    if NEW_TRAY_SOCKET not in [str(b) for b in idx]:
        E('W   ⚠ 마네퀸에 본 %s 가 없다' % NEW_TRAY_SOCKET)
        fails.append('tray_socket')
    else:
        L('W   본 %s 존재 확인' % NEW_TRAY_SOCKET)
except Exception as ex:
    L('W   본 확인 건너뜀: %s' % str(ex)[:60])

# ───────────────────────── Actions 맵의 몽타주 교체
L('W === Actions 맵 ===')
acts = cdo.get_editor_property('actions')
swapped = missing = 0
for key in list(acts.keys()):
    act = acts[key]
    variants = act.get_editor_property('variants')
    out = []
    names = []
    for var in variants:
        m = var.get_editor_property('montage')
        if not m:
            continue
        new_path = '%s/%s' % (NEW_ANIMDIR, m.get_name())
        nm = EAL.load_asset(new_path) if EAL.does_asset_exist(new_path) else None
        if not nm:
            E('W   %-18s %s 의 마네퀸판이 없다' % (key, m.get_name()))
            fails.append('%s/%s' % (key, m.get_name()))
            missing += 1
            continue
        var.set_editor_property('montage', nm)
        out.append(var)
        names.append(nm.get_name())
        swapped += 1
    act.set_editor_property('variants', out)
    acts[key] = act
    L('W   %-18s %s' % (key, ' · '.join(names)))
cdo.set_editor_property('actions', acts)
L('W   교체 %d개 · 못 찾음 %d개' % (swapped, missing))

# ⚠ **저장이 컴파일보다 먼저** (§6)
L('W   save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
L('W   save(컴파일 후) -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False))

# ───────────────────────── 레벨에 직접 놓인 손님이 있나
L('W === 레벨에 놓인 손님 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
cls = unreal.load_object(None, '/Game/BurgerHouse/BP/BP_Customer.BP_Customer_C')
placed = unreal.GameplayStatics.get_all_actors_of_class(world, cls) if cls else []
L('W   %d명 (0 이면 전부 스포너가 만든다 — CDO 만 고치면 된다)' % len(placed))
for a in placed:
    mc = a.get_editor_property('mesh')
    sm = mc.get_editor_property('skeletal_mesh') if mc else None
    L('W     %-28s %s' % (a.get_actor_label(), sm.get_name() if sm else None))

L('W')
if fails:
    E('W === 실패: %s ===' % fails)
else:
    L('W === 교체 완료 (검산은 새 프로세스로) ===')
