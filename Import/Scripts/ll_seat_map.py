"""좌석 지도 — `GroupId` 가 실제로 어느 테이블인지 찍는다 (읽기 전용)

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_seat_map.py" -AllowCommandletRendering

⚠ 에디터가 열려 있으면 에디터 안 Python 콘솔에서:
    exec(open(r'C:/Project/Unreal/Import/Scripts/ll_seat_map.py').read())

## 왜 필요한가

`PREFILL_TABLES` 는 테이블을 **`GroupId` 번호**로 지목한다. 그런데 그 번호는
`ll_slots.py` 의 `cluster_tables()` 가 **union-find 가 클러스터를 발견한 순서**
대로 매긴 것이라, 「서쪽 위에서 몇 번째」 같은 사람 기준과 아무 관계가 없다.

⚠ **추측하면 안 된다.** 번호를 잘못 짚으면 손님이 엉뚱한 테이블에 앉는데,
  그건 눈으로만 보이고 로그에는 아무 경고도 안 나온다.

이 스크립트는 **아무것도 바꾸지 않는다.** 읽어서 찍기만 한다.

## 좌표 읽는 법

UE 값을 블렌더로 되돌려 같이 찍는다 — 설계 문서(`LL_PROJECT.md`)와 블렌더
씬이 전부 미터·+y 북쪽 기준이라 그쪽이 사람이 알아보기 쉽다.

    블렌더 = (X/100, −Y/100)

## 방향 약속

문(남쪽, 블렌더 y≈0)에서 **안쪽(북, +y)을 바라본 기준**으로 적는다:

    앞/뒤(S/N) = y 작은 쪽 / 큰 쪽
    좌/우       = x 작은 쪽 / 큰 쪽
"""
import unreal

LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
M = 100.0


def to_blender(v):
    """UE 월드 → 블렌더 미터. `ll_place.py` 변환의 역이다."""
    return (v.x / M, -v.y / M, v.z / M)


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)

    slots = [a for a in eas.get_all_level_actors()
             if isinstance(a, unreal.SlotPoint)]
    if not slots:
        unreal.log_error('SlotPoint 가 하나도 없다 — ll_slots.py 를 먼저 돌릴 것')
        return

    # ---- 종류별 집계 ----
    kinds = {}
    for s in slots:
        k = str(s.get_editor_property('kind'))
        kinds[k] = kinds.get(k, 0) + 1
    unreal.log_warning('=== 슬롯 %d개 ===' % len(slots))
    unreal.log_warning('  ' + ' · '.join('%s %d' % (k, kinds[k]) for k in sorted(kinds)))

    # ---- 좌석을 테이블(GroupId)별로 ----
    tables = {}
    for s in slots:
        if str(s.get_editor_property('kind')) != 'Seat':
            continue
        gid = int(s.get_editor_property('group_id'))
        tables.setdefault(gid, []).append(s)

    unreal.log_warning('')
    unreal.log_warning('=== 좌석 테이블 %d개 (블렌더 미터) ===' % len(tables))
    unreal.log_warning('  문에서 안쪽(+y)을 본 기준 — 좌/우 = x 작은/큰, 앞/뒤 = y 작은/큰')

    # 테이블을 **사람이 찾기 쉬운 순서**로 늘어놓는다 — 서쪽부터, 그 안에서 남쪽부터.
    rows = []
    for gid, seats in tables.items():
        pts = [to_blender(s.get_actor_location()) for s in seats]
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        rows.append((cx, cy, gid, seats))
    rows.sort(key=lambda r: (round(r[0], 1), r[1]))

    for cx, cy, gid, seats in rows:
        # 서쪽 부스(x<2)인지 중간 테이블(x>2)인지
        where = '서쪽 부스' if cx < 2.0 else '중간 테이블'
        # Wall_W_A(y 1.44~4.96) / Wall_W_B(y 7.08~11.02)
        wall = ''
        if cx < 2.0:
            wall = ' · Wall_W_A' if cy < 6.0 else ' · Wall_W_B'
        unreal.log_warning('')
        unreal.log_warning('  [GroupId %d]  %s%s  — %d석  중심 (%.2f, %.2f)'
                           % (gid, where, wall, len(seats), cx, cy))

        # 좌석은 앞(y작은)→뒤 순, 같으면 좌(x작은)→우
        info = sorted(((to_blender(s.get_actor_location()), s) for s in seats),
                      key=lambda t: (round(t[0][1], 2), t[0][0]))
        for (bx, by, bz), s in info:
            ue = s.get_actor_location()
            use = s.get_use_transform().translation
            unreal.log_warning('      %-22s 블렌더 (%.3f, %.3f)   UE (%.0f, %.0f)   사용지점 UE (%.0f, %.0f)'
                               % (s.get_actor_label(), bx, by, ue.x, ue.y, use.x, use.y))

    # ---- 예약 안 하는 지점도 같이 ----
    unreal.log_warning('')
    unreal.log_warning('=== 그 외 지점 ===')
    for s in slots:
        k = str(s.get_editor_property('kind'))
        if k == 'Seat':
            continue
        bx, by, _ = to_blender(s.get_actor_location())
        ue = s.get_actor_location()
        unreal.log_warning('  %-10s %-16s 블렌더 (%.3f, %.3f)   UE (%.0f, %.0f)'
                           % (k, s.get_actor_label(), bx, by, ue.x, ue.y))

    unreal.log_warning('')
    unreal.log_warning('=== 완료 — 아무것도 바꾸지 않았다 ===')


main()
