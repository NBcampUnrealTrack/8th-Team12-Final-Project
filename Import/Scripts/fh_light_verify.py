"""FlowerHouse 조명 검산 — 새 프로세스로. 종류·모빌리티·세기 합계."""
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level('/Game/Levels/FlowerHouse/FlowerHouse')
rows = {}
for a in eas.get_all_level_actors():
    lc = a.get_component_by_class(unreal.LightComponent)
    if lc is None:
        continue
    k = (a.get_class().get_name(), str(lc.mobility).split('.')[-1])
    r = rows.setdefault(k, [0, 0.0])
    r[0] += 1
    r[1] += lc.get_editor_property('intensity')
for k, (n, s) in sorted(rows.items()):
    unreal.log_warning('[FHL] %-18s %-10s %2d등  intensity 합 %.0f' % (k[0], k[1], n, s))
