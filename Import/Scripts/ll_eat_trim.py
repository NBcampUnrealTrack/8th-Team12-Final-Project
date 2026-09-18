"""먹는 모션을 `Eating_a_Burger` · `Eating_Fries` 둘로 줄인다.

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_eat_trim.py"

`AM_Eating_Frantically` · `AM_Eating_a_Drumstick` 을 `BP_Customer ▸ Actions` 의
모든 변형 목록에서 빼고, 몽타주와 원본 애님 4개를 지운다.

## 어디를 고치나

`Actions` 는 `TMap<FName, FCustomerAction>` 이고 `FCustomerAction.Variants` 가
`TArray<FCustomerActionVariant>` (`Montage` · `Weight` · `bShowPhone`) 다
(`CustomerCharacter.h:68`). 둘이 들어 있는 곳은 두 군데다 —

    Sit_Eat   AM_Eating_* 4개 (각 1.0)              ← 트리(워크인) 손님
    Sit_Idle  위 4개 + Sitting_Talking · SitIdle_Male  ← 장식(프리필) 손님

`DecorationActions` 는 `TMap<FName, FName>` (역할 -> 행동 이름) 이라 몽타주를
직접 안 들고 있다. 손댈 필요가 없다.

`ST_Customer` 는 행동 **이름**(`Sit_Eat` 등)만 부르고 몽타주를 모른다. 변형만
빠지므로 트리는 그대로 둔다.

## ⚠ 저장을 컴파일보다 먼저

`Actions` 는 CDO 기본값이라 컴파일 없이 저장해도 반영된다. 컴파일을 먼저 하면
저장이 조용히 실패한 적이 있다 (BURGERHOUSE.md §저장).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/BP/BP_Customer'
DROP = ('AM_Eating_Frantically', 'AM_Eating_a_Drumstick')
ASSETS = (
    '/Game/BurgerHouse/Anims/Manny/Montages/AM_Eating_Frantically',
    '/Game/BurgerHouse/Anims/Manny/Montages/AM_Eating_a_Drumstick',
    '/Game/BurgerHouse/Anims/Manny/Eating_Frantically',
    '/Game/BurgerHouse/Anims/Manny/Eating_a_Drumstick',
)

bp = EAL.load_asset(BP)
if not bp:
    raise RuntimeError('%s 가 없다' % BP)
cdo = unreal.get_default_object(bp.generated_class())

# ── 1. 현재 상태 ────────────────────────────────────────────────────
L('=== 1. 손대기 전 Actions ===')
actions = cdo.get_editor_property('Actions')
for key in actions.keys():
    names = []
    for v in actions[key].get_editor_property('Variants'):
        m = v.get_editor_property('Montage')
        names.append('%s %.1f' % (m.get_name() if m else 'None',
                                  v.get_editor_property('Weight')))
    L('  %-18s %s' % (key, ' · '.join(names)))

# ── 2. 변형에서 제거 ────────────────────────────────────────────────
L('')
L('=== 2. 제거 ===')
removed = 0
new_actions = unreal.Map(unreal.Name, unreal.CustomerAction)
for key in actions.keys():
    act = actions[key]
    keep = []
    for v in act.get_editor_property('Variants'):
        m = v.get_editor_property('Montage')
        if m and m.get_name() in DROP:
            L('  %-18s - %s' % (key, m.get_name()))
            removed += 1
        else:
            keep.append(v)
    new_act = unreal.CustomerAction()
    new_act.set_editor_property('Variants', keep)
    new_actions[key] = new_act

cdo.set_editor_property('Actions', new_actions)
L('  변형 %d 개 제거' % removed)

# ⚠ 저장 -> 컴파일 -> 저장 순서 (문서 §저장)
pkg = bp.get_outermost()
L('  save=%s' % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
L('  compile 후 save=%s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

# ── 3. 에셋 삭제 ────────────────────────────────────────────────────
L('')
L('=== 3. 에셋 삭제 ===')
for p in ASSETS:
    if not EAL.does_asset_exist(p):
        L('  %-58s 이미 없다' % p)
        continue
    refs = EAL.find_package_referencers_for_asset(p, False)
    refs = [r for r in refs if not any(a.rsplit('/', 1)[-1] == r.rsplit('/', 1)[-1]
                                       for a in ASSETS)]
    if refs:
        E('  %-58s 아직 참조됨: %s' % (p, ', '.join(refs)))
        continue
    L('  %-58s 삭제=%s' % (p, EAL.delete_asset(p)))

L('')
L('※ 검산은 새 프로세스에서 ll_eat_trim_verify.py 로 한다')
