"""팔이 너무 벌어진 것을 **리타겟 포즈**로 오므린다.

    에디터를 **끄고** 돌린다. 뒤이어 `ll_tray_hands.py` 를 돌릴 것.

## 어디를 돌리는 게 맞나

애님을 하나씩 고치는 게 아니라 **리타게터의 타깃 리타겟 포즈**를 돌린다.

```
출력 본 회전 ≈ 타깃_리타겟포즈 ∘ (소스_현재 ∘ 소스_리타겟포즈⁻¹)
```

타깃 리타겟 포즈의 팔을 오므리면 **모든 애님이 같이** 오므라든다. 애님 24개를
따로 만지는 것보다 손잡이가 하나라 되돌리기도 쉽다.

## ⚠⚠ 좌우 부호는 축마다 다르다 — 실험으로 정했다

`upperarm_l` 에 +X, `upperarm_r` 에 −X 로 **거울처럼** 주는 것이 자연스러워 보이지만
`pitch` 는 그렇지 않았다. 실측(`Idle` 의 손 간격) —

```
기준                       handL.x  39.4 · handR.x −40.6 · 간격 80.0
pitch 를 거울로 ±20         한쪽만 들어오고 다른 쪽은 나갔다 (간격 78.1, 거의 그대로)
pitch 를 **양쪽 −20**       handL.x  23.3 · handR.x −25.0 · 간격 48.5
```

⚠ 그래서 **양쪽에 같은 부호**를 준다. roll·yaw 는 팔을 앞뒤로 돌려서 원하는
  그림이 아니었다.

## 얼마나 오므릴까 — 1도당 손 간격 약 1.6cm

```
pitch −10  간격 64.7   (−15.3)
pitch −20  간격 48.5   (−31.5)
pitch −30  간격 32.0   (−48.1)
pitch −40  간격 16.0   (−64.1)
```

거의 선형이다. **−12** 를 기본으로 잡았다 (간격 80 → 약 61). 더 오므리려면 이
숫자 하나만 바꾸고 다시 돌리면 된다.

⚠ `Carrying` 의 손 간격도 같이 줄어든다 (62.3 → 약 43). 트레이 긴 축이 40 이라
  **오히려 잘 맞는다.** 대신 `TrayHandOffset` 을 다시 재야 하므로
  `ll_tray_hands.py` 를 이어서 돌릴 것.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

RTG = '/Game/BurgerHouse/Anims/Manny/Retarget/RTG_MixamoToManny'
SRC_DIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'
SRC_MESH = '/Game/BurgerHouse/Anims/Mixamo/Hip_Hop_Dancing'
TGT_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
OUT = '/Game/BurgerHouse/Anims/Manny'
TMP = '/Game/BurgerHouse/Anims/_ArmTest'

# ⚠ 양쪽 같은 부호. 음수가 오므리는 쪽이다 (모듈 주석의 실측표).
ARM_PITCH = -12.0

rtg = EAL.load_asset(RTG)
rc = unreal.IKRetargeterController.get_controller(rtg)

L('A === 타깃 리타겟 포즈: 위팔 pitch %+.0f ===' % ARM_PITCH)
for b in ('upperarm_l', 'upperarm_r'):
    rc.set_rotation_offset_for_retarget_pose_bone(
        b, unreal.Rotator(0.0, ARM_PITCH, 0.0).quaternion(),
        unreal.RetargetSourceOrTarget.TARGET)
    got = rc.get_rotation_offset_for_retarget_pose_bone(b, unreal.RetargetSourceOrTarget.TARGET)
    L('A   %-12s -> %s' % (b, got.rotator()))
EAL.save_asset(RTG, only_if_is_dirty=False)

# ───────────────────────── 애님 시퀀스만 다시 굽는다
L('A')
L('A === 애님 시퀀스 다시 굽기 ===')
# ⚠ **ABP 와 몽타주는 빼고** 시퀀스만 한다. 그쪽을 다시 구우면 참조가 옛 Mixamo 로
#   되돌아간다 (§9-19 의 그 함정). 시퀀스는 **제자리에 덮어써지므로** 몽타주·ABP 가
#   그대로 새 내용을 본다.
arr = unreal.Array(unreal.AssetData)
names = []
for a in AR.get_assets_by_path(SRC_DIR, recursive=True):
    if str(a.asset_class_path.asset_name) != 'AnimSequence':
        continue
    arr.append(a)
    names.append(str(a.asset_name))
L('A   대상 %d개' % len(arr))

i = unreal.IKRetargetBatchOperationInputs()
i.set_editor_property('assets_to_retarget', arr)
i.set_editor_property('source_mesh', EAL.load_asset(SRC_MESH))
i.set_editor_property('target_mesh', EAL.load_asset(TGT_MESH))
i.set_editor_property('ik_retarget_asset', rtg)
i.set_editor_property('use_source_path', False)
i.set_editor_property('overwrite_existing_files', True)
i.set_editor_property('include_referenced_assets', False)
i.set_editor_property('target_path', OUT)
unreal.IKRetargetBatchOperation.run_batch_retarget(i)

# ⚠⚠ 반환값에 기대지 않는다 — 폴더의 더티 패키지를 전부 저장한다 (§9-19).
pkgs = []
for p in EAL.list_assets(OUT, recursive=False, include_folder=False):
    o = EAL.load_asset(p)
    if o:
        pkgs.append(o.get_outermost())
L('A   저장 %d개 -> %s'
  % (len(pkgs), unreal.EditorLoadingAndSavingUtils.save_packages(pkgs, only_dirty=True)))

# ───────────────────────── 실측
L('A')
L('A === 확인: Idle 의 손 간격 ===')
import math
AL = unreal.AnimationLibrary


def comp(anim, bone, t):
    acc = unreal.Transform()
    for b in reversed(list(AL.find_bone_path_to_root(anim, bone))):
        acc = unreal.MathLibrary.compose_transforms(
            AL.get_bone_pose_for_time(anim, str(b), t, False), acc)
    return acc


for nm in ('Idle', 'Carrying', 'Walk_Fwd_Manny'):
    p = '%s/%s' % (OUT, nm)
    if not EAL.does_asset_exist(p):
        continue
    a = EAL.load_asset(p)
    hl = comp(a, 'hand_l', 0.0).translation
    hr = comp(a, 'hand_r', 0.0).translation
    L('A   %-16s handL.x %6.1f · handR.x %6.1f · 간격 %.1f'
      % (nm, hl.x, hr.x, math.sqrt((hl.x - hr.x) ** 2 + (hl.y - hr.y) ** 2 + (hl.z - hr.z) ** 2)))

# ───────────────────────── 실험용 임시 폴더 치우기
L('A')
n = 0
for p in EAL.list_assets(TMP, recursive=True, include_folder=False):
    EAL.delete_asset(p.split('.')[0])
    n += 1
if n:
    EAL.delete_directory(TMP)
L('A   임시 폴더 정리 %d개' % n)
L('A')
L('A ⚠ 이어서 `ll_tray_hands.py` 를 돌릴 것 — Carrying 의 손 간격이 바뀌었다.')
