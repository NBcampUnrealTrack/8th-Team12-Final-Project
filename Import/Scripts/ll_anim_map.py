"""양쪽 chime 을 0.02 로 통일하고, **어떤 상황에 어떤 애님이 도는지** 전부 훑는다.

    에디터를 **끄고** 돌린다.

## 왜 이 스크립트가 있나

애님이 정해지는 곳이 **넷**이라 한눈에 안 보인다 —

| 정하는 곳 | 무엇을 정하나 |
|---|---|
| `ST_Customer` 의 `Play Action` | 상태별 **한 번 돌고 끝나는** 몸짓 |
| `BP_Customer ▸ Actions` | 그 이름이 **어떤 몽타주들**로 풀리나 |
| `BP_Customer ▸ DecorationActions` | **장식 손님**(트리를 안 타는 프리필)의 몸짓 |
| `UpdateLocomotionMontage` | 서기/걷기/앉기 — **속도와 상태**로 매 틱 고른다 |

⚠ 넷이 서로 다른 층이라, 하나만 보면 늘 틀린 답이 나온다.
"""
import unreal

L = unreal.log_warning
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService
ST = '/Game/BurgerHouse/BP/ST_Customer'
BP = '/Game/BurgerHouse/BP/BP_Customer'

# ───────────────────────── chime 통일
L('A === chime 0.02 로 통일 ===')
for st, what in (('Root/Kiosk/K_Order', '주문 넣을 때'),
                 ('Root/Pickup', '주문 나왔을 때')):
    before = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
              for p in (S.get_task_property_names(ST, st, 'BurgerHousePlaySoundTask') or [])}
    r = S.set_task_property_value(ST, st, 'BurgerHousePlaySoundTask', 'VolumeMultiplier', '0.02')
    L('A   %-20s %-14s %s -> 0.02  %s'
      % (st.split('/')[-1], what, before.get('VolumeMultiplier'), r))

pkg = EAL.load_asset(ST).get_outermost()
L('A   save %s / compile %s / save %s' % (
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False),
    S.compile_state_tree(ST).get_editor_property('success'),
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)))

# ───────────────────────── 애님 지도
cdo = unreal.get_default_object(EAL.load_asset(BP).generated_class())

L('')
L('A === 1) 트리의 Play Action — 상태별 몸짓 ===')
info = S.get_state_tree_info(ST)
for st in info.get_editor_property('all_states'):
    path = str(st.get_editor_property('path'))
    for t in (st.get_editor_property('tasks') or []):
        if 'BurgerHouseActionTask' not in str(t.get_editor_property('struct_type')):
            continue
        d = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
             for p in (S.get_task_property_names(ST, path, 'BurgerHouseActionTask') or [])}
        L('A   %-34s Action=%-18s 기본시간=%-6s 추가반복=%s'
          % (path, d.get('ActionName'), d.get('FallbackSeconds'), d.get('RandomExtraRepeats')))

L('')
L('A === 2) Actions 맵 — 이름이 풀리는 몽타주 ===')
acts = cdo.get_editor_property('actions')
for k in sorted(acts.keys(), key=str):
    v = acts[k]
    names = []
    for var in v.get_editor_property('variants'):
        m = var.get_editor_property('montage')
        try:
            w = var.get_editor_property('weight')
        except Exception:
            w = '?'
        names.append('%s(%s)' % (m.get_name() if m else 'None', w))
    L('A   %-18s %s' % (k, ' · '.join(names)))

L('')
L('A === 3) DecorationActions — 장식 손님(트리를 안 탄다) ===')
try:
    dec = cdo.get_editor_property('decoration_actions')
    for k in sorted(dec.keys(), key=str):
        L('A   역할 %-12s -> %s' % (k, dec[k]))
except Exception as e:
    L('A   못 읽음: %s' % e)
for p in ('b_decoration_single_action', 'decoration_single_action', 'decoration_seed'):
    try:
        L('A   %-28s %s' % (p, cdo.get_editor_property(p)))
    except Exception:
        pass

L('')
L('A === 4) 로코모션 — 속도·상태로 매 틱 고른다 ===')
for p in ('loco_idle_action', 'loco_walk_action', 'loco_sit_action',
          'loco_carry_walk_action', 'loco_carry_idle_action',
          'loco_walk_speed', 'loco_speed_hysteresis'):
    try:
        L('A   %-24s %s' % (p, cdo.get_editor_property(p)))
    except Exception:
        L('A   %-24s <없음>' % p)

L('')
L('A === 5) 스포너의 프리필 구성 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for sp in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CustomerSpawner):
    try:
        pl = sp.get_editor_property('prefill_placements')
        L('A   PrefillPlacements %d개' % len(pl))
        for i, e in enumerate(pl):
            L('A     %d  역할 %-10s 장식 %s'
              % (i, e.get_editor_property('start_role'), e.get_editor_property('b_decoration')))
    except Exception as e:
        L('A   %s' % e)
