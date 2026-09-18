"""스폰 주기 변경이 **디스크에 남았는지** 새 프로세스에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리를 보는 것이라 의미가 없다 (§6). 레벨 액터
  프로퍼티는 특히 그렇다 — `save_current_level()` 이 `True` 를 주고도 안 쓴다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
WANT = {'spawn_interval': 240.0, 'spawn_interval_jitter': 30.0}

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

fails = []
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    L('=== %s ===' % sp.get_name())
    for p, w in WANT.items():
        got = sp.get_editor_property(p)
        ok = abs(got - w) < 0.01
        L('  %-22s %7.1f  %s' % (p, got, 'OK' if ok else '<-- ⚠ 기대 %.1f' % w))
        if not ok:
            fails.append('%s 가 %.1f 다 (기대 %.1f)' % (p, got, w))

    lo = sp.get_editor_property('spawn_interval') - sp.get_editor_property('spawn_interval_jitter')
    hi = sp.get_editor_property('spawn_interval') + sp.get_editor_property('spawn_interval_jitter')
    L('  실제 주기 범위 %.0f ~ %.0f초 (%.0f%% 흔들림)'
      % (lo, hi, sp.get_editor_property('spawn_interval_jitter')
         / max(sp.get_editor_property('spawn_interval'), 1e-6) * 100.0))

    # ⚠ 주기보다 큰 지터는 최소 0.05초로 잘린다 (헤더 경고)
    if sp.get_editor_property('spawn_interval_jitter') >= sp.get_editor_property('spawn_interval'):
        fails.append('지터가 주기보다 크다 — 주기가 0.05초로 잘릴 수 있다')

    # 프리필은 안 건드렸어야 한다
    pl = sp.get_editor_property('prefill_placements')
    L('  PrefillPlacements %d개 · DesiredAlive %d (그대로여야 한다)'
      % (len(pl), sp.get_editor_property('fixed_desired_alive')))
    if len(pl) != 5:
        fails.append('PrefillPlacements 가 %d개다 (기대 5)' % len(pl))

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 — 디스크에 남았다 ===')
