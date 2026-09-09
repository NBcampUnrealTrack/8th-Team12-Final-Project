"""NavMesh 검증 — 「홀만 초록인가」를 눈이 아니라 숫자로

    ⚠ **에디터에서 `Build > Build Paths` 를 먼저 돌린 뒤** 에디터 안
      Python 콘솔에서 실행할 것:

        exec(open(r'C:/Project/Unreal/Import/Scripts/ll_nav_check.py').read())

    커맨드릿(`-run=pythonscript`)에서는 내비를 굽지 않으므로 **전부 실패로
    나온다.** 그건 내비가 없다는 뜻이지 배치가 틀렸다는 뜻이 아니다.

`UE_NPC_PLAN.md` Phase 2 의 통과 기준을 그대로 잰다.

## 무엇을 재나

**① 커버리지** — 홀 바닥에 격자로 점을 찍고 각 점이 NavMesh 에 올라오는지 본다.
   `project_point_to_navigation` 이 실패하면 그 자리는 못 걷는 곳이다.

**② 막아야 할 곳이 실제로 막혔나** — 주방·바 테이블 밑을 따로 찍어 본다.
   여기가 초록이면 블로커가 안 먹은 것이다.

**③ 슬롯 옆에 설 자리가 있나** — 좌석·키오스크·픽업 41개의 **둘레**를 돌며
   내비를 찾는다.

⚠⚠ **좌석 지점 자체를 투영하면 안 된다.** 의자가 거기 서 있고 콜리전이 그
  자리를 파내므로 **당연히 실패**한다. NPC 는 좌석에 걸어 들어가는 게 아니라
  옆까지 와서 앉는 애님으로 정렬된다. 처음에 이걸 틀려 36/41 이 「못 닿음」
  으로 나왔다 — **검사가 틀린 것이지 배치가 틀린 게 아니었다.**

⚠ 커버리지 100% 를 기대하면 안 된다. 테이블·의자·카운터가 서 있는 자리는
  당연히 빠진다. **막아야 할 곳이 막혔는지**와 **슬롯 옆에 설 수 있는지**가
  본질이다.
"""
import unreal

LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
M = 100.0
STEP = 25.0                      # 격자 간격 (cm)

# ⚠ **투영 허용 범위는 좁아야 한다.** `project_point_to_navigation` 은 이
#   범위 안에서 가장 가까운 내비 표면을 찾는다. XY 를 60 으로 두면 **60cm
#   옆에 내비가 있기만 해도 그 점을 「걷는 곳」으로 세어** 커버리지가 부풀고,
#   막힌 곳도 뚫린 것처럼 나온다.
#   Z 는 넉넉해야 한다 — 바닥 높이가 조금씩 다르다.
EXTENT = unreal.Vector(25, 25, 120)


def to_ue(x, y, z=0.0):
    """블렌더 미터 -> UE (ll_place.py 와 같은 규약)"""
    return unreal.Vector(x * M, -y * M, z * M)


# 걸어야 하는 곳 — 블렌더 미터 (x0,y0,x1,y1)
WALKABLE = ('홀', (0.30, 0.20, 5.70, 11.20))

# 막혀야 하는 곳
BLOCKED = [
    ('주방',        (0.50, 11.80, 5.50, 13.40)),
    ('바 테이블 밑', (0.50, 0.15, 2.50, 0.50)),
]


_FAILED = []


def project(nav, world, p):
    """점 하나를 NavMesh 에 투영. 올라가면 True.

    ⚠ **인자를 위치로 넘기면 안 된다.** 시그니처가

        project_point_to_navigation(world_context_object, point, nav_data,
                                    filter_class, query_extent=(0,0,0))

      이라 네 번째는 `filter_class` 다. 여기에 `query_extent` 를 넘기면
      `TypeError: Failed to convert parameter 'filter_class'` 가 난다.
      **반환도 `(bool, Vector)` 가 아니라 `Vector or None`** 이다.

    ⚠⚠ **예외를 삼키지 않는다.** 처음에 `except: return False` 로 감싸 놨더니
      위 `TypeError` 가 전부 「그 자리는 못 걷는다」로 둔갑해 **990/990 실패**,
      즉 「내비가 안 구워졌다」는 **거짓 진단**이 나왔다. 실제로는 내비가
      구워져 있었고 호출이 틀렸을 뿐이다. 첫 예외는 **크게 찍고 중단**한다.
    """
    try:
        r = unreal.NavigationSystemV1.project_point_to_navigation(
            world, p, nav_data=nav, filter_class=None, query_extent=EXTENT)
        return r is not None
    except Exception as e:
        if not _FAILED:
            _FAILED.append(e)
            unreal.log_error('project_point_to_navigation 호출 실패: %s: %s'
                             % (type(e).__name__, e))
        return False


def sample(nav, world, box, z=10.0):
    x0, y0, x1, y1 = box
    hit = tot = 0
    misses = []
    yy = y0
    while yy <= y1:
        xx = x0
        while xx <= x1:
            tot += 1
            if project(nav, world, to_ue(xx, yy, z / M)):
                hit += 1
            elif len(misses) < 6:
                misses.append((round(xx, 2), round(yy, 2)))
            xx += STEP / M
        yy += STEP / M
    return hit, tot, misses


def main():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = ues.get_editor_world()

    # ⚠⚠ **여기서 레벨을 다시 로드하면 안 된다.** `load_level` 은 월드를
    #   새로 만들고, 그러면 방금 `Build > Build Paths` 로 구운 **내비 데이터가
    #   통째로 날아간다.** 그래 놓고 "내비가 안 구워졌다"고 보고하게 된다 —
    #   처음에 실제로 그랬다 (`LogCrowdFollowing: Unable to find RecastNavMesh`).
    #   이미 열려 있는 레벨을 쓰고, 다른 레벨이면 **거부**한다.
    cur = world.get_path_name() if world else ''
    if LEVEL.split('/')[-1] not in cur:
        unreal.log_error('현재 레벨이 %s 가 아니다 (%s). 에디터에서 그 레벨을'
                         ' 열고 `Build > Build Paths` 한 뒤 다시 실행할 것.'
                         % (LEVEL, cur))
        return
    nav = None
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.RecastNavMesh):
            nav = a
            break
    if nav is None:
        unreal.log_error('RecastNavMesh 가 없다 — ll_nav.py 를 먼저')
        return

    unreal.log_warning('=== NavMesh 검증 ===')

    name, box = WALKABLE
    hit, tot, miss = sample(nav, world, box)
    pct = 100.0 * hit / max(tot, 1)
    unreal.log_warning('  [걸려야 함] %-12s %d/%d = %.1f%%' % (name, hit, tot, pct))

    # ⚠ 호출 자체가 깨졌으면 **그 사실을 먼저** 말한다. 안 그러면 「내비가
    #   안 구워졌다」는 엉뚱한 결론으로 사람을 보낸다 (실제로 그랬다).
    if _FAILED:
        unreal.log_error('  ⚠⚠ 투영 API 호출이 실패했다 — 위 결과는 전부 무의미하다.')
        unreal.log_error('     내비 상태와 무관하다. 스크립트를 고칠 것.')
        return

    # ⚠ **내비가 아예 없으면 「막혀야 함」이 전부 0% 라 거짓 OK 가 된다.**
    #   그래서 「구워졌는가」를 먼저 판정해야 하는데 —
    #   ⚠⚠ **커버리지 %로 판정하면 안 된다.** 처음에 `>= 50%` 로 뒀더니
    #   가구가 빽빽한 이 매장의 **정상값 43.7%** 를 「내비 없음」으로 읽어
    #   멀쩡한 결과(블로커 0/18, 슬롯 32/41)를 전부 `판정 불가` 로 가려 버렸다.
    #   내비가 없으면 **단 한 점도** 안 잡힌다 — 그게 유일하게 확실한 신호다.
    built = hit > 0
    if not built:
        unreal.log_warning('     ⚠ **내비가 안 구워졌다** (한 점도 안 잡혔다).')
        unreal.log_warning('       에디터에서 `Build > Build Paths` 를 먼저 할 것.')
        unreal.log_warning('       (커맨드릿은 내비를 굽지 않는다)')
    elif pct < 15.0:
        unreal.log_warning('     ⚠ 커버리지가 낮다 — 바운즈가 작거나 블로커가'
                           ' 과한지 볼 것 (가구가 많으면 40%대가 정상이다)')
    elif miss:
        unreal.log_warning('     못 걷는 지점 예: %s (가구 자리면 정상)' % miss)

    for name, box in BLOCKED:
        hit, tot, _ = sample(nav, world, box)
        pct2 = 100.0 * hit / max(tot, 1)
        verdict = ('판정 불가 (내비 없음)' if not built
                   else ('OK' if pct2 < 5.0 else '**안 막힘**'))
        unreal.log_warning('  [막혀야 함] %-12s %d/%d = %.1f%%  %s'
                           % (name, hit, tot, pct2, verdict))

    # ---- 막는 볼륨이 실제로 어디를 덮고 있나 ----
    # ⚠ 「안 막힘」이 나왔을 때 **볼륨이 작은 건지 영역 클래스가 안 먹은 건지**
    #   구별하려면 실제 경계를 봐야 한다.
    for a in eas.get_all_level_actors():
        if not isinstance(a, unreal.NavModifierVolume):
            continue
        o, ext = a.get_actor_bounds(False)
        ac = a.get_editor_property('area_class')
        unreal.log_warning('  [블로커] %-20s x %7.1f~%7.1f y %8.1f~%8.1f'
                           '  area=%s'
                           % (a.get_actor_label(), o.x - ext.x, o.x + ext.x,
                              o.y - ext.y, o.y + ext.y,
                              ac.get_name() if ac else '**없음**'))

    # ---- 슬롯 **옆에** 설 자리가 있나 ----
    # ⚠⚠ **좌석 지점 자체는 NavMesh 위에 있을 수 없다.** 의자가 거기 서 있고
    #   콜리전이 그 자리를 파낸다. NPC 는 좌석에 **걸어 들어가는** 게 아니라
    #   옆까지 와서 앉는 애님으로 정렬된다 (`UE_NPC_PLAN.md` 「착석 정렬」).
    #   그래서 좌석 지점을 그대로 투영하면 **전부 실패**로 나온다 —
    #   처음에 그래서 36/41 이 「못 닿음」으로 찍혔다.
    #   물어야 할 것은 **「이 좌석 옆에 설 수 있는가」** 다. 둘레를 돌며 본다.
    # ⚠ 합격/불합격이 아니라 **거리**를 잰다. 「붙박이 소파는 좌석 옆에 설
    #   자리가 없는 게 정상」이라 O/X 로는 판단이 안 된다 — NPC 는 통로까지
    #   걸어와서 앉는 애님으로 미끄러져 들어간다. 물어야 할 것은
    #   **「가장 가까운 통로가 몇 cm 인가」** 다.
    cls = unreal.load_class(None, '/Script/Febricula.SlotPoint')
    slots = [a for a in eas.get_all_level_actors()
             if cls and a.get_class() == cls]
    BIG = unreal.Vector(250, 250, 150)
    rows = []
    for s in slots:
        t = s.call_method('GetUseTransform')
        c = t.translation
        d = None
        try:
            r = unreal.NavigationSystemV1.project_point_to_navigation(
                world, unreal.Vector(c.x, c.y, 10.0), nav_data=nav,
                filter_class=None, query_extent=BIG)
            if r is not None:
                d = ((r.x - c.x) ** 2 + (r.y - c.y) ** 2) ** 0.5
        except Exception as e:
            unreal.log_error('  거리 측정 실패: %s' % e)
            break
        rows.append((s.get_actor_label(),
                     str(s.get_editor_property('kind')), c, d))

    far = [r for r in rows if r[3] is None or r[3] > 100.0]
    near = [r for r in rows if r[3] is not None and r[3] <= 100.0]
    ds = sorted(r[3] for r in near)
    unreal.log_warning('  [슬롯 -> 통로 거리] %d개 측정' % len(rows))
    if ds:
        unreal.log_warning('     100cm 이내 %d개 — 최소 %.0f / 중앙 %.0f / 최대 %.0f cm'
                           % (len(ds), ds[0], ds[len(ds) // 2], ds[-1]))
    bad = [r[0] for r in far]
    dirs = {}
    if far and built:
        unreal.log_warning('     ⚠ 100cm 초과 %d개 — 애님 워프 거리를 넘는다'
                           % len(far))
        for label, kind, c, d in sorted(far, key=lambda r: r[0]):
            unreal.log_warning('        %-18s %-7s %s  블렌더 (%.2f, %.2f)'
                               % (label, kind,
                                  '%5.0f cm' % d if d else ' 못 찾음',
                                  c.x / M, -c.y / M))
        unreal.log_warning('     ⚠ **붙박이 소파는 이게 정상일 수 있다** — 좌석'
                           ' 옆이 아니라 테이블 반대쪽에서 접근한다.')
        unreal.log_warning('       의자가 여기 들어 있으면 통로가 막힌 것이니'
                           ' 배치를 볼 것.')

    unreal.log_warning('=== 끝 ===')


main()
