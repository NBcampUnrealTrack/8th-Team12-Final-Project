"""인터랙션 슬롯 배치 — 좌석 / 키오스크 / 픽업

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_slots.py" -AllowCommandletRendering

⚠ 에디터가 열려 있으면 에디터 안 Python 콘솔에서:
    exec(open(r'C:/Project/Unreal/Import/Scripts/ll_slots.py').read())

`UE_NPC_PLAN.md` Phase 4.

## 왜 슬롯 액터를 따로 두나

의자 29개·키오스크 2대는 UE 에서 **HISM 인스턴스**다. 인스턴스에는 컴포넌트를
붙일 수 없고, 예약(claim)은 액터 단위로 걸어야 한다. NPC 는 의자를 **앉기만**
하므로 렌더 메쉬와 인터랙션 앵커가 같은 액터일 이유가 없다.
→ 좌석 트랜스폼마다 **메쉬 없는 `ASlotPoint`** 를 놓는다. 렌더 비용 0.

## 좌표

`LL_placement.json` 의 인스턴스를 그대로 읽어 `ll_place.py` 와 **같은 축 변환**
을 쓴다. 두 곳이 어긋나면 슬롯이 가구에서 떨어지므로 변환식을 복사하지 말고
같은 규약을 지킬 것.

    UE 위치 = (x, −y, z) × 100 / 회전 Roll +rx, Pitch −ry, Yaw −rz

## 앉는 지점 — 원점 그대로 쓰면 안 된다

⚠ **의자 원점에 그대로 앉히면 등받이에 파묻힌다.** 의자 원점은 바닥 중심이고
  앉는 지점은 거기서 **앞쪽(정면 축 −Y, 로컬 기준)** 으로 조금, 위로 좌면
  높이만큼 떨어져 있다. `LocalOffset` 에 그 값을 넣어 둔다.

  가구 정면 축은 블렌더 −Y = UE +Y 다 (`TODO.md` 확정). `ASlotPoint` 의
  로컬 오프셋은 **액터 로컬**이므로 +Y 쪽으로 밀면 앉는 사람 앞쪽이 된다.
"""
import unreal
import json
import os

JSON = r'C:/Project/Unreal/Import/LL_placement.json'
LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
PREFIX = 'SLOT_'
M = 100.0

# `ll_place.py` 와 **같은** 축 변환
AXIS = {
    'loc': lambda x, y, z: unreal.Vector(x * M, -y * M, z * M),
    'rot': lambda rx, ry, rz: unreal.Rotator(pitch=-ry, yaw=-rz, roll=rx),
}

# 에셋 -> (슬롯 종류, 로컬 오프셋 cm)
#   오프셋은 **액터 로컬**. +Y 가 가구 정면(앉는 사람이 보는 쪽)이다.
# ⚠⚠ **좌석의 Z 는 0 이다 — 좌면 높이(45)를 넣지 말 것.**
#
#   앉는 애님이 **골반을 좌면 높이로 올려 준다.** 여기에 또 45 를 넣으면
#   캡슐이 45 올라가고 그 위에서 애님이 또 45 를 올려 **90cm 에 앉은 그림**이
#   된다. 처음엔 45 를 넣었다가 손님이 의자 위에 뜨는 것을 보고 고쳤다.
#
#   미세 조정은 `ACustomerCharacter::SitZAdjust` (에디터에서 눈으로).
#
# ⚠ Z 를 0 으로 두면 `Move To` 목적지도 바닥이 되어 **내비 투영도 잘 된다** —
#   45cm 공중을 목적지로 주면 투영이 실패할 수 있었다.
SPEC = {
    # 의자 — 앉는 지점은 원점에서 살짝 앞. **Z 는 0**
    'Chair_A':   ('Seat',  unreal.Vector(0.0, 6.0, 0.0)),
    # 소파 — 유닛 원점이 모서리라 폭의 절반만큼 안으로
    'Sofa_Unit': ('Seat',  unreal.Vector(0.0, 18.0, 0.0)),
    # 키오스크 — 앞에 서는 지점 (60cm 앞)
    'Kiosk_A':   ('Kiosk', unreal.Vector(0.0, 60.0, 0.0)),
}

# ⚠⚠ **가구 정면과 캐릭터 정면이 다르다 — 그런데 가구마다 다르다.**
#
# UE 캐릭터 정면은 **+X** 다. 좌석 슬롯은 가구 회전을 그대로 물려받으므로
# 가구 정면이 +X 가 아니면 손님이 옆을 보고 앉는다.
#
#     Chair_A     정면 = 로컬 **+Y**  ->  +90 을 줘야 한다
#     Sofa_Unit   정면 = 로컬 **+X**  ->  보정 없음 (0)
#
# ⚠⚠ **의자만 보고 「좌석은 전부 +90」 이라고 단정하지 말 것.** 처음에 그렇게
#   했다가 소파 8석이 벽을 보고 앉았다. 실측 —
#
#     의자 29석: 액터 로컬 +Y 가 테이블 (+X 는 85~95° 어긋남)
#     소파  8석: 액터 로컬 **+X** 로 56cm 앞이 테이블, 옆으로 y≈0
#                (그 너머 110cm 에 마주보는 Chair_A — 부스 배치)
#
# 그래서 이 표의 키는 **종류(kind)가 아니라 에셋 이름**이다.
#
# ⚠ 아래 `KIOSK`·`PICKUP`·`DOOR` 목록은 **UE yaw 를 손으로 적어** 두므로
#   보정이 필요 없다 (0). 가구 회전을 상속하는 것에만 준다.
#
# ⚠ `LocalOffset` 은 이 값의 영향을 안 받는다 — 오프셋은 가구 기준으로 이미
#   맞춰져 있다 (둘 다 +Y 쪽이 앉는 사람 앞).
USE_YAW_OFFSET = {
    'Chair_A':   90.0,
    'Sofa_Unit':  0.0,
    'Kiosk_A':    0.0,
}

# 카운터 픽업 — 인스턴스가 아니라 직접 지정한다 (블렌더 미터)
# 카운터 앞면이 y 11.368, 파티션이 11.398 이므로 그 앞 1m 쯤에 선다.
#
# ⚠ **UE yaw 로 적는다.** 카운터는 블렌더 +y(북)이고 UE 로는 −Y 이므로
#   카운터를 마주보려면 **yaw −90** 이다. +90 으로 두면 등을 돌린다.
PICKUP = [
    ('Pickup_L', (2.20, 10.40, 0.0), -90.0),
    ('Pickup_R', (2.90, 10.40, 0.0), -90.0),
]

# ---------------------------------------------------------------- 문
#
# 손님이 **생기는 자리**(Entry)와 **지워지는 자리**(Exit).
#
# ⚠ `Entry`/`Exit` 는 **예약하지 않는다.** 들어오는 사람과 나가는 사람이 동시에
#   쓰므로 `ClaimSlot` 이 아니라 `FindPoint` 로 조회만 한다. 예약을 걸면 문이
#   한 명에게 잠겨 뒤에 오는 손님이 영영 못 들어온다.
#
# ## 두 버전 — `OUTSIDE` 스위치
#
# **False (지금 쓰는 것) — 문지방 안쪽.**
#   내비 바운즈가 블렌더 y 0.00 에서 시작하므로(`ll_nav.py`) 바깥에는 내비가
#   없다. 안쪽 y 0.55 면 에이전트 반지름 34cm 를 빼도 여유가 있다.
#   ⚠ 손님이 문 안쪽에서 **팝 하고 나타난다.** 임시 상태다.
#
# **True — 문 바깥 포치.**
#   `ll_porch.py` 로 임시 바닥판 + 내비 바운즈를 깔아야 쓸 수 있다.
#   ⚠⚠ 그것만으로는 부족하다 — **유리문에 콜리전이 있으면 내비가 문을 못
#     뚫는다** (`LL_PROJECT.md` §6.4 "반투명 쪽에도 콜리전을 준다"). 그래서
#     `ll_porch.py` 가 실제로 경로를 테스트해서 알려준다.
OUTSIDE = False

# 문 개구부 — `Front_Door_L` x 2.828 · `Front_Door_R` x 4.572 (둘 다 y 0.075)
# 2차선으로 나눠 들어오는 사람과 나가는 사람이 정면충돌하지 않게 한다.
#
# ⚠ **UE yaw.** 안쪽(블렌더 +y)을 보려면 −90, 바깥(−y)을 보려면 +90.
DOOR_IN = [
    ('Entry', (4.20, 0.55, 0.0), -90.0),   # 들어와서 안쪽을 본다
    ('Exit',  (3.20, 0.55, 0.0),  90.0),   # 나가려고 바깥을 본다
]

DOOR_OUT = [
    ('Entry', (4.20, -0.90, 0.0), -90.0),  # 포치 위. ll_porch.py 필요
    ('Exit',  (3.20, -0.90, 0.0),  90.0),
]

DOOR = DOOR_OUT if OUTSIDE else DOOR_IN

# ---------------------------------------------------------------- 테이블 묶기
#
# ⚠⚠ **`Table_A` 는 테이블이 아니라 0.403m 짜리 상판 모듈이다.**
#   `Table_C1`(x 2.998) 과 `Table_C2`(x 3.401) 는 **같은 y**에 0.403m 간격으로
#   붙어 **한 테이블**을 이룬다. 벽쪽 `Table_WA1~3` 은 y 로 0.403m 씩 이어져
#   3모듈짜리 긴 테이블 하나다.
#
#   모듈 21개를 그대로 세면 "테이블 21개"가 되지만 **앉는 단위로는 10개**다.
#   일행을 「같은 테이블」에 앉히려면 이 묶음이 진짜 단위여야 한다.
#
# 붙어 있다고 볼 거리(m). 모듈 간격 0.403 보다 크고, 다음 테이블까지의 최소
# 간격(0.856) 보다 작아야 한다. 그 사이면 아무 값이나 같은 결과가 나온다.
TABLE_LINK_DIST = 0.55

# 좌석을 테이블에 붙일 최대 거리(m). 이보다 멀면 `GroupId = -1` (소속 없음).
#   벽쪽: 테이블 x 0.80 ↔ 소파 x 0.24 / 의자 x 1.32  → 약 0.56
#   가운데: 테이블 x 2.998~3.401 ↔ 의자 x 2.965/3.435 → 약 0.04~0.44
SEAT_TO_TABLE_MAX = 1.20


def cluster_tables(tables):
    """붙어 있는 모듈끼리 묶는다 (union-find).

    `tables` — [(name, x, y), ...]
    반환      — [(중심x, 중심y, [이름...]), ...]  인덱스가 곧 `GroupId`
    """
    n = len(tables)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for i in range(n):
        for j in range(i + 1, n):
            dx = tables[i][1] - tables[j][1]
            dy = tables[i][2] - tables[j][2]
            if (dx * dx + dy * dy) ** 0.5 <= TABLE_LINK_DIST:
                union(i, j)

    groups = {}
    for i in range(n):
        groups.setdefault(find(i), []).append(i)

    out = []
    for members in groups.values():
        cx = sum(tables[m][1] for m in members) / len(members)
        cy = sum(tables[m][2] for m in members) / len(members)
        out.append((cx, cy, [tables[m][0] for m in members]))
    # y → x 순으로 정렬해 번호가 매번 같게 한다 (재실행 시 재현성)
    out.sort(key=lambda t: (round(t[1], 3), round(t[0], 3)))
    return out


def main():
    if not os.path.exists(JSON):
        unreal.log_error('배치 JSON 이 없다: %s' % JSON)
        return
    with open(JSON, encoding='utf-8') as f:
        data = json.load(f)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)

    # 다시 돌릴 때를 위해 이전 슬롯을 지운다
    old = [a for a in eas.get_all_level_actors()
           if a.get_actor_label().startswith(PREFIX)]
    for a in old:
        eas.destroy_actor(a)
    unreal.log_warning('=== 인터랙션 슬롯 ===')
    unreal.log_warning('  기존 %d개 제거' % len(old))

    cls = unreal.load_class(None, '/Script/Febricula.SlotPoint')
    if cls is None:
        unreal.log_error('  ASlotPoint 를 못 찾았다 — C++ 빌드를 먼저 할 것')
        return

    # ---- 테이블 묶기 ----
    # 모듈을 붙어 있는 것끼리 묶어 **앉는 단위**를 만든다 (위 주석).
    modules = [(i['name'], i['loc'][0], i['loc'][1])
               for i in data['instances'] if i.get('asset') == 'Table_A']
    tables = cluster_tables(modules)
    unreal.log_warning('  테이블 — 모듈 %d개 → 묶음 %d개'
                       % (len(modules), len(tables)))
    for gi, (cx, cy, names) in enumerate(tables):
        unreal.log_warning('    [%d] 중심 (%.2f, %.2f)  모듈 %d개  %s'
                           % (gi, cx, cy, len(names), ', '.join(sorted(names))))

    def nearest_table(x, y):
        """가장 가까운 테이블 묶음 번호. 너무 멀면 -1."""
        best, best_d = -1, 1e9
        for gi, (cx, cy, _) in enumerate(tables):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d < best_d:
                best, best_d = gi, d
        return best if best_d <= SEAT_TO_TABLE_MAX else -1

    made = {}
    per_table = {}
    for inst in data['instances']:
        asset = inst.get('asset')
        if asset not in SPEC:
            continue
        kind, offset = SPEC[asset]
        lx, ly, lz = inst['loc']
        rx, ry, rz = inst['rot_deg']
        loc = AXIS['loc'](lx, ly, lz)
        rot = AXIS['rot'](rx, ry, rz)
        a = eas.spawn_actor_from_class(cls, loc, rot)
        if a is None:
            unreal.log_error('  스폰 실패: %s' % inst['name'])
            continue
        idx = made.get(kind, 0)
        a.set_actor_label('%s%s_%02d' % (PREFIX, kind, idx))
        a.set_editor_property('kind', unreal.Name(kind))
        a.set_editor_property('local_offset', offset)
        a.set_editor_property('use_yaw_offset', USE_YAW_OFFSET.get(asset, 0.0))
        a.set_editor_property('source_asset', unreal.Name(asset))
        a.set_editor_property('source_index', idx)

        # ⚠ **좌석만** 테이블에 묶는다. 키오스크는 테이블과 무관하다.
        #    거리는 **의자 원점** 기준이다 — 사용 지점(`LocalOffset` 적용)은
        #    앉는 사람 앞쪽으로 밀려 있어 테이블에서 오히려 멀어질 수 있다.
        gid = nearest_table(lx, ly) if kind == 'Seat' else -1
        a.set_editor_property('group_id', gid)
        if kind == 'Seat':
            per_table[gid] = per_table.get(gid, 0) + 1

        made[kind] = idx + 1

    # ---- 검산 — 테이블마다 몇 자리인가 ----
    unreal.log_warning('  좌석 배정:')
    for gi in sorted(per_table):
        n = per_table[gi]
        if gi < 0:
            unreal.log_error('    ⚠ 소속 없음 %d석 — SEAT_TO_TABLE_MAX 를 볼 것' % n)
        else:
            mark = ''
            if n < 2:
                mark = '  ⚠ 1석짜리 — 일행이 앉을 수 없다'
            elif n > 6:
                mark = '  ⚠ 너무 많다 — 묶음이 잘못됐을 수 있다'
            unreal.log_warning('    [%d] %d석%s' % (gi, n, mark))

    # ---- 픽업 지점 ----
    for name, (px, py, pz), yaw in PICKUP:
        loc = AXIS['loc'](px, py, pz)
        # ⚠ `unreal.Rotator(a, b, c)` 는 **(roll, pitch, yaw)** 다. 위치 인자로
        #   `(0, yaw, 0)` 을 주면 **pitch** 에 들어가 고개만 숙인다 —
        #   실제로 그렇게 나와 Pickup 이 yaw 0 이었다. 키워드로 준다.
        a = eas.spawn_actor_from_class(cls, loc,
                                       unreal.Rotator(roll=0.0, pitch=0.0,
                                                      yaw=yaw))
        if a is None:
            continue
        idx = made.get('Pickup', 0)
        a.set_actor_label('%sPickup_%02d' % (PREFIX, idx))
        a.set_editor_property('kind', unreal.Name('Pickup'))
        a.set_editor_property('source_asset', unreal.Name('Counter'))
        a.set_editor_property('group_id', -1)
        a.set_editor_property('source_index', idx)
        made['Pickup'] = idx + 1

    # ---- 문 — Entry / Exit ----
    # 픽업과 같은 방식이지만 **종류가 이름 그대로**다 (Entry/Exit 은 하나씩).
    for kind, (px, py, pz), yaw in DOOR:
        loc = AXIS['loc'](px, py, pz)
        a = eas.spawn_actor_from_class(cls, loc,
                                       unreal.Rotator(roll=0.0, pitch=0.0,
                                                      yaw=yaw))
        if a is None:
            continue
        a.set_actor_label('%s%s' % (PREFIX, kind))
        a.set_editor_property('kind', unreal.Name(kind))
        a.set_editor_property('source_asset', unreal.Name('Door'))
        a.set_editor_property('group_id', -1)
        a.set_editor_property('source_index', 0)
        made[kind] = made.get(kind, 0) + 1

    unreal.log_warning('  문 지점: %s (OUTSIDE=%s)'
                       % ('포치 바깥' if OUTSIDE else '문지방 안쪽', OUTSIDE))
    if OUTSIDE:
        unreal.log_warning('    ⚠ ll_porch.py 를 먼저 돌려야 내비가 있다')

    les.save_current_level()
    unreal.log_warning('  만든 슬롯: %s (합계 %d)'
                       % (dict(sorted(made.items())), sum(made.values())))

    # ---- 검산 — 슬롯이 가구 위에 있는가 ----
    slots = [a for a in eas.get_all_level_actors()
             if a.get_actor_label().startswith(PREFIX)]
    unreal.log_warning('  레벨 확인 %d개' % len(slots))
    for s in sorted(slots, key=lambda x: x.get_actor_label())[:4]:
        p = s.get_actor_location()
        unreal.log_warning('     %-18s (%7.1f %8.1f %6.1f) yaw %6.1f'
                           % (s.get_actor_label(), p.x, p.y, p.z,
                              s.get_actor_rotation().yaw))
    unreal.log_warning('=== 완료 — 레벨 저장됨 ===')
    unreal.log_warning('  ⚠ 좌석 오프셋(`LocalOffset`)은 애님이 붙은 뒤 눈으로'
                       ' 맞출 것 — 지금은 좌면 높이 45cm 로 잡아 뒀다.')


main()
