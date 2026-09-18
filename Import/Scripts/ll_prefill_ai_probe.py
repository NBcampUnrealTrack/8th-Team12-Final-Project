"""프리필 손님에게 **AI 컨트롤러가 정말 안 붙는지** 확인한다. 안 고친다.

## 의심하는 것

`Interact_Reach`(손 뻗기)·`Idle_Look`(폰 보기)은 **StateTree 전용 액션**이다.
`ll_st_dump.py` 로 확인했듯 트리의 `Enter`·`K_Act` 가 쓰고, 장식 손님이 도는
`Sit_Idle` 풀에는 없다. 그런데 화면에 나온다면 —

    ① 프리필에게도 컨트롤러가 붙어 트리가 돌고 있거나
    ② 그 손님이 사실 워크인(걸어 들어온 손님)이거나

①이면 원인은 **`AutoPossessAI`** 다. 스포너는 `bPrefillRunsAI=false` 일 때
`SpawnDefaultController()` 를 **안 부르는 것**으로 AI 를 막는다. 그런데
`AutoPossessAI` 가 `PlacedInWorldOrSpawned` 면 엔진이 `PostInitializeComponents`
에서 **알아서** 컨트롤러를 띄운다 — 스포너의 억제가 통째로 무력해진다.

그러면 트리와 장식 루프가 **같은 DefaultSlot 을 두고 싸운다.** 트리가
`Idle_Look`/`Interact_Reach` 를 걸면 잠깐 그게 보이고, 2.35초 타이머가 돌아오면
`Sit_Idle` 이 덮는다 — 「중간에 1초쯤 다른 동작이 스친다」가 된다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/BP/BP_Customer'
cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP))

L('=== ① BP_Customer 의 AI 설정 ===')
for p in ('auto_possess_ai', 'ai_controller_class', 'auto_possess_player'):
    try:
        L('  %-24s %s' % (p, cdo.get_editor_property(p)))
    except Exception as ex:
        E('  %-24s 못 읽음: %s' % (p, str(ex)[:60]))

L('')
L('  ⚠ AutoPossessAI 가 PLACED_IN_WORLD_OR_SPAWNED 면 엔진이 스폰 직후')
L('    컨트롤러를 띄운다 — 스포너의 bPrefillRunsAI=false 가 무력해진다.')
L('    DISABLED 라야 SpawnDefaultController() 를 부른 쪽만 AI 가 돈다.')

L('')
L('=== ② 스포너의 프리필 설정 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    for p in ('prefill_runs_ai', 'prefill_on_begin_play', 'fixed_desired_alive',
              'spawn_interval', 'npc_class'):
        try:
            L('  %-24s %s' % (p, sp.get_editor_property(p)))
        except Exception as ex:
            E('  %-24s 못 읽음: %s' % (p, str(ex)[:60]))

L('')
L('=== ③ 트리가 쓰는 액션 vs 장식이 쓰는 액션 ===')
deco = cdo.get_editor_property('decoration_actions')
L('  DecorationActions: %s' % {str(k): str(deco[k]) for k in deco.keys()})
actions = cdo.get_editor_property('actions')
for k in actions.keys():
    names = [v.get_editor_property('montage').get_name()
             for v in actions[k].get_editor_property('variants')
             if v.get_editor_property('montage')]
    tag = ''
    if str(k) in ('Interact_Reach', 'Interaction_Reach', 'Idle_Look'):
        tag = '  <-- 트리 전용 (장식은 이걸 쓸 경로가 없다)'
    if str(k) == 'Sit_Idle':
        tag = '  <-- 장식(프리필) 전용'
    L('  %-18s %s%s' % (k, ', '.join(names), tag))

L('')
L('=== ④ 장식 루프의 재생 주기 (덮어쓰기 간격) ===')
cf = cdo.get_editor_property('decoration_loop_crossfade')
for k in actions.keys():
    if str(k) != 'Sit_Idle':
        continue
    for v in actions[k].get_editor_property('variants'):
        m = v.get_editor_property('montage')
        if not m:
            continue
        d = m.get_editor_property('sequence_length')
        L('  %-24s %6.2fs -> %6.2f초마다 다시 건다' % (m.get_name(), d, max(d - cf, 0.25)))
L('  ⚠ 트리가 끼어들어도 이 주기 안에 Sit_Idle 이 덮는다 — 그래서 「1초쯤」 스친다.')
