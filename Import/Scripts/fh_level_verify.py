"""FlowerHouse 레벨 검산 — **새 프로세스**로 (저장이 진짜 됐는지는 이것만 믿는다)"""
import unreal
import json

LEVEL = '/Game/Levels/FlowerHouse/FlowerHouse'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(LEVEL)
acts = eas.get_all_level_actors()
by = {}
inst = 0
hism = 0
empty_mesh = []
for a in acts:
    c = a.get_class().get_name()
    by[c] = by.get(c, 0) + 1
    for h in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
        inst += h.get_instance_count()
        hism += 1
    if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh is None:
        empty_mesh.append(a.get_actor_label())
sma = by.get('StaticMeshActor', 0)
d = json.load(open(r'C:/Project/Unreal/Import/FH_placement.json', encoding='utf-8'))
unreal.log_warning('[FHV] 액터 %d개: %s' % (len(acts), dict(sorted(by.items()))))
unreal.log_warning('[FHV] HISM %d개 인스턴스 %d + 단일 %d = %d  (JSON %d)  메쉬 빈 액터 %d'
                   % (hism, inst, sma, inst + sma, len(d['instances']), len(empty_mesh)))
t = eas.get_actor_reference('FH_Table_Slab') if hasattr(eas, 'get_actor_reference') else None
for a in acts:
    if a.get_actor_label() in ('FH_Table_Slab', 'FH_Book_Godot', 'FH_PlayerStart'):
        o, e = a.get_actor_bounds(False)
        unreal.log_warning('[FHV] %s 중심 %s 크기 %s' % (a.get_actor_label(),
                           [round(v) for v in (o.x, o.y, o.z)], [round(v) for v in (e.x, e.y, e.z)]))
