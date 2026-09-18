"""프리필 손님의 idle 을 `SitIdle_Male` 로 바꾸기 전 **사실 확인**. 안 고친다.

보는 것 —
  ① `SitIdle_Male` 3벌의 스켈레톤·길이·골반 높이 (앉은 동작인가?)
  ② `BP_Customer > Actions` 의 `Sit_Idle` · `Idle_Look` 에 뭐가 꽂혀 있나
  ③ `DecorationActions` 매핑
  ④ 레벨의 `CustomerSpawner` 가 프리필로 무슨 **역할**을 낳는가
     -> 서 있는 역할(Waiting·Ordering)이 있으면 앉은 애님을 씌우면 안 된다
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

# 앉음/섬 판정 기준 (§7 에서 쓴 것과 같다)
SIT_Z, STAND_Z = 63.0, 95.0

CANDIDATES = [
    '/Game/Characters/ChairAnimation/Male/SitIdle_Male',
    '/Game/MetaHumans/MHC_Character_Adam/Animation/SitIdle_Male',
    '/Game/MetaHumans/MHC_Character_Adam/Cheon/Anim/SitIdle_Male',
    # 비교용 — 손님이 지금 쓰는 것
    '/Game/BurgerHouse/Anims/Mixamo/Anims/Idle',
    '/Game/BurgerHouse/Anims/Mixamo/Anims/Sitting',
    '/Game/BurgerHouse/Anims/Eat_Animation_Pack/Eating_a_Burger_Anim',
]

ROOTS = ('root', 'pelvis', 'hips', 'Hips')


def probe(path):
    if not EAL.does_asset_exist(path):
        E('  %-56s 없다' % path.replace('/Game/', ''))
        return
    a = EAL.load_asset(path)
    sk = a.get_editor_property('skeleton')
    dur = a.get_editor_property('sequence_length')
    try:
        tracks = [str(t) for t in unreal.AnimationLibrary.get_animation_track_names(a)]
    except Exception:
        tracks = []

    # 골반 트랙을 찾아 높이를 본다 — 앉음 57~63 · 섬 103 (§7)
    low = {t.lower(): t for t in tracks}
    pel = None
    for c in ROOTS:
        if c.lower() in low:
            # root 는 보통 0 이므로 그 다음(골반)을 쓴다
            if c.lower() == 'root' and len(tracks) > 1:
                pel = tracks[1]
            else:
                pel = low[c.lower()]
            break
    z = None
    if pel:
        p = unreal.AnimationLibrary.get_bone_pose_for_time(a, pel, 0.0, False)
        z = p.translation.z

    verdict = '?'
    if z is not None:
        verdict = '앉음' if z < SIT_Z else ('섬' if z > STAND_Z else '중간')
    L('  %-52s %-26s %6.3fs  트랙%3d  골반(%s) Z %6.1f  %s'
      % (path.replace('/Game/', ''), sk.get_name() if sk else '(없음)',
         dur, len(tracks), pel, z if z is not None else -1, verdict))


L('=== ① SitIdle 후보 · 비교군 ===')
for p in CANDIDATES:
    probe(p)

L('')
L('=== ② BP_Customer > Actions ===')
bp = EAL.load_asset('/Game/BurgerHouse/BP/BP_Customer')
cdo = unreal.get_default_object(bp.generated_class())
actions = cdo.get_editor_property('actions')
for key in actions.keys():
    act = actions[key]
    try:
        variants = act.get_editor_property('variants')
    except Exception:
        L('  %-16s (변형을 못 읽음)' % key)
        continue
    parts = []
    for v in variants:
        m = v.get_editor_property('montage')
        w = v.get_editor_property('weight')
        parts.append('%s %.1f' % (m.get_name() if m else '(없음)', w))
    L('  %-16s %s' % (key, ' · '.join(parts)))

L('')
L('=== ③ DecorationActions ===')
for k, v in cdo.get_editor_property('decoration_actions').items():
    L('  %-12s -> %s' % (k, v))
L('  bDecorationSingleAction = %s' % cdo.get_editor_property('decoration_single_action'))

L('')
L('=== ④ L_BurgerHouse 의 CustomerSpawner ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
acts = unreal.GameplayStatics.get_all_actors_of_class(
    unreal.EditorLevelLibrary.get_editor_world(), unreal.CustomerSpawner)
if not acts:
    E('  CustomerSpawner 를 못 찾았다')
for sp in acts:
    L('  %s' % sp.get_name())
    for p in ('fixed_desired_alive', 'hard_max_alive', 'prefill_on_begin_play',
              'prefill_runs_ai'):
        try:
            L('      %-24s %s' % (p, sp.get_editor_property(p)))
        except Exception as ex:
            E('      %-24s 못 읽음 %s' % (p, str(ex)[:50]))
    try:
        for r in sp.get_editor_property('prefill_roles'):
            L('      PrefillRoles   %s' % r)
    except Exception as ex:
        E('      prefill_roles 못 읽음: %s' % str(ex)[:60])
    try:
        pl = sp.get_editor_property('prefill_placements')
        L('      PrefillPlacements %d개' % len(pl))
        for i, p in enumerate(pl):
            L('        [%d] %s' % (i, p))
    except Exception as ex:
        E('      prefill_placements 못 읽음: %s' % str(ex)[:60])

L('')
L('=== 끝 — 아무것도 안 고쳤다 ===')
