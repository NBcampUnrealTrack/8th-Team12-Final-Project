"""Album 액터가 무엇인지 — 메시·트랜스폼·슬롯·주변을 찍는다.

CD 팩처럼 꾸미려면 **어느 면이 앞면인지**를 알아야 하는데, 큐브는 대칭이라
이름만으로는 정할 수 없다.

⚠ 두께 축은 스케일의 **절댓값**으로 봐야 한다. 이 액터는 Z 스케일이 음수
  (-0.211) 라 그냥 정렬하면 Z 가 가장 얇은 축으로 잡힌다 — 틀렸다.
  실제 두께 축은 Y (0.018) 다.

앞면은 큰 면(±Y) 중 **열린 쪽**이다. 주변 액터를 찍어서 어느 쪽이 벽인지 본다.
"""
import unreal
import math

L = unreal.log_warning
LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(LEVEL)

actors = eas.get_all_level_actors()
hits = [a for a in actors
        if 'album' in a.get_actor_label().lower() or 'album' in a.get_name().lower()]

for a in hits:
    t = a.get_actor_transform()
    loc, rot, scl = t.translation, a.get_actor_rotation(), t.scale3d
    L('=== %s (%s) ===' % (a.get_actor_label(), a.get_class().get_name()))
    L('  loc   = (%.1f, %.1f, %.1f)' % (loc.x, loc.y, loc.z))
    L('  rot   = (pitch %.1f, yaw %.1f, roll %.1f)' % (rot.pitch, rot.yaw, rot.roll))
    L('  scale = (%.3f, %.3f, %.3f)' % (scl.x, scl.y, scl.z))

    # ⚠ 절댓값으로 두께 축을 고른다
    axes = sorted(zip(('X', 'Y', 'Z'), (scl.x, scl.y, scl.z)),
                  key=lambda kv: abs(kv[1]))
    thin = axes[0][0]
    L('  -> 두께 축 = %s (%.3f) · 큰 면은 ±%s' % (thin, axes[0][1], thin))

    # 로컬 ±두께축이 월드에서 어디를 향하는가
    q = t.rotation
    local_dir = {'X': unreal.Vector(1, 0, 0),
                 'Y': unreal.Vector(0, 1, 0),
                 'Z': unreal.Vector(0, 0, 1)}[thin]
    n = q.rotate_vector(local_dir)
    L('  월드에서 로컬 +%s = (%.3f, %.3f, %.3f)' % (thin, n.x, n.y, n.z))

    # 양쪽 30cm 앞 지점에서 가장 가까운 액터까지의 거리 — 벽 쪽은 가깝다
    for sign, tag in ((1.0, '+' + thin), (-1.0, '-' + thin)):
        probe = unreal.Vector(loc.x + n.x * 30 * sign,
                             loc.y + n.y * 30 * sign,
                             loc.z + n.z * 30 * sign)
        near = []
        for o in actors:
            if o is a:
                continue
            try:
                org, ext = o.get_actor_bounds(False)
            except Exception:
                continue
            if ext.x <= 0 and ext.y <= 0 and ext.z <= 0:
                continue
            # 점에서 AABB 까지의 거리
            dx = max(abs(probe.x - org.x) - ext.x, 0.0)
            dy = max(abs(probe.y - org.y) - ext.y, 0.0)
            dz = max(abs(probe.z - org.z) - ext.z, 0.0)
            d = math.sqrt(dx * dx + dy * dy + dz * dz)
            near.append((d, o.get_actor_label()))
        near.sort()
        L('  %s 쪽 30cm 지점 최근접: %s' % (tag, ', '.join(
            '%s(%.0f)' % (lbl, d) for d, lbl in near[:5])))

    L('')
    L('  === 반경 120cm 이웃 ===')
    for o in actors:
        if o is a:
            continue
        ol = o.get_actor_location()
        d = math.sqrt((ol.x - loc.x) ** 2 + (ol.y - loc.y) ** 2 + (ol.z - loc.z) ** 2)
        if d < 120:
            L('    %6.1fcm  %-28s %s  @(%.0f, %.0f, %.0f)'
              % (d, o.get_actor_label(), o.get_class().get_name(), ol.x, ol.y, ol.z))

L('')
L('=== PlayerStart / 손님 동선 기준점 ===')
for o in actors:
    cn = o.get_class().get_name()
    if 'PlayerStart' in cn or 'Spawner' in cn or 'PlayerStart' in o.get_actor_label():
        ol = o.get_actor_location()
        L('  %-28s %-24s @(%.0f, %.0f, %.0f)' % (o.get_actor_label(), cn, ol.x, ol.y, ol.z))
