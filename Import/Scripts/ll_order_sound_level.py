"""레벨의 `AmbientSound` 를 주문 알림용으로 배선한다.

    에디터를 **끄고** 돌린다.

하는 것 둘 —
  ① 액터 `Tags` 에 `OrderChime` 을 넣는다 (태스크가 이걸로 찾는다)
  ② `AudioComponent.bAutoActivate` 를 **끈다**

## ⚠ `Auto Activate` 를 왜 끄나

`AAmbientSound` 는 기본이 「레벨이 열리면 계속 운다」다. 그대로 두면 주문과
상관없이 처음부터 울고 있어서, 태스크가 `Play()` 를 불러도 **달라지는 게 없다.**
「트리거가 안 먹는다」로 보이지만 원인은 이쪽이다.

## ⚠⚠ Sound 에셋은 **안 건드린다**

무엇을 울릴지는 취향이라 고르지 않는다. 지금 비어 있고, 비어 있으면 태스크가
경고를 찍고 조용히 넘어간다 (`LogBurgerHouseST`).

## ⚠ 레벨 액터 프로퍼티는 조용히 안 써진다 (§6)

`save_packages(..., only_dirty=False)` 로 저장하고 **새 프로세스**로 검산한다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
TAG = 'OrderChime'

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
acts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AmbientSound)

if not acts:
    E('AmbientSound 가 레벨에 없다')
    raise SystemExit

pkgs = []
for a in acts:
    L('=== %s (%s) ===' % (a.get_actor_label(), a.get_name()))

    # ① 태그
    tags = [str(t) for t in a.tags]
    if TAG not in tags:
        arr = unreal.Array(unreal.Name)
        for t in tags:
            arr.append(unreal.Name(t))
        arr.append(unreal.Name(TAG))
        a.set_editor_property('tags', arr)
    L('  Tags  %s -> %s' % (tags, [str(t) for t in a.tags]))

    # ② Auto Activate
    ac = a.get_editor_property('audio_component')
    if ac:
        before = ac.get_editor_property('auto_activate')
        ac.set_editor_property('auto_activate', False)
        L('  AutoActivate  %s -> %s' % (before, ac.get_editor_property('auto_activate')))
        L('  Sound         %s' % ac.get_editor_property('sound'))
        if not ac.get_editor_property('sound'):
            L('  ⚠ Sound 가 비어 있다 — 붙이기 전까지는 소리가 안 난다 (의도적으로 안 골랐다)')
    else:
        E('  AudioComponent 가 없다')

    pkgs.append(a.get_outermost())

ok = unreal.EditorLoadingAndSavingUtils.save_packages(list(set(pkgs)), only_dirty=False)
L('')
L('저장 save_packages(only_dirty=False) -> %s' % ok)
if not ok:
    E('⚠⚠ 레벨이 디스크에 안 써졌다 — 아래 되읽기는 메모리다')
