"""NavMesh — 손님 NPC 가 걸어다닐 바닥을 만든다

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_nav.py" -AllowCommandletRendering

⚠ 에디터가 열려 있으면 에디터 안 Python 콘솔에서:
    exec(open(r'C:/Project/Unreal/Import/Scripts/ll_nav.py').read())

`UE_NPC_PLAN.md` Phase 2.

## 무엇을 만드나

| | 역할 |
|---|---|
| `NavMeshBoundsVolume` | **홀만** 감싼다. 이게 없으면 NavMesh 자체가 없다 |
| `RecastNavMeshActor` | 실제 내비 데이터. 바운즈를 놓으면 자동 생성되지만 에이전트 값을 손대려면 직접 잡아야 한다 |
| `NavModifierVolume` (Null) | **못 가는 곳**을 판다 — 주방 · 카운터 안쪽 · 바 테이블 밑 |

## 좌표 — 블렌더 미터를 UE 로

    UE = (x, −y, z) × 100          (`ll_place.py` 의 AXIS 와 같은 규약)

홀은 블렌더 x 0~6 / y 0~11.4 → UE x 0~600 / y −1140~0.
주방은 블렌더 y > 11.4 → UE y < −1140.

⚠ **`NavMeshBoundsVolume` 은 브러시라 크기를 `set_actor_scale3d` 로 준다.**
  기본 브러시가 200×200×200 이므로 원하는 크기 ÷ 200 이 스케일이다.
  (`BrushComponent` 를 직접 다시 만드는 것보다 이 쪽이 훨씬 덜 깨진다.)

⚠ **셸 콜리전은 complex-as-simple** 이라 렌더 메쉬가 그대로 벽이다 —
  NavMesh 가 벽·카운터를 제대로 인식한다. 다만 **HISM 가구의 UCX** 는
  자동 껍질이 하나씩 더 붙어 있어(`UE_IMPORT.md` 「남은 흠」) 의자 밑이
  막힐 수 있다. 생성 후 `P` 로 눈으로 볼 것.
"""
import unreal

LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
M = 100.0
BRUSH = 200.0           # 기본 큐브 브러시 한 변


def ue_box(x0, y0, z0, x1, y1, z1):
    """블렌더 미터 범위 -> (UE 중심, UE 크기). y 는 부호가 뒤집힌다."""
    ux0, ux1 = x0 * M, x1 * M
    uy0, uy1 = -y1 * M, -y0 * M          # ⚠ 뒤집으면 min/max 도 자리를 바꾼다
    uz0, uz1 = z0 * M, z1 * M
    center = unreal.Vector((ux0 + ux1) / 2, (uy0 + uy1) / 2, (uz0 + uz1) / 2)
    size = unreal.Vector(abs(ux1 - ux0), abs(uy1 - uy0), abs(uz1 - uz0))
    return center, size


# 이름, 블렌더 범위 (x0,y0,z0, x1,y1,z1)
#
# ⚠ **실측으로 정했다.** 처음에 「카운터 안쪽」을 y 10.30~11.45 로 막으려다
#   `Cnt_Partition` 이 y 11.398~11.548, `Cnt_Counter` 앞면이 y 11.368 인 것을
#   확인하고 걷어냈다 — 그 1.1m 는 **손님이 줄 서는 자리**다. 하마터면
#   대기 구역을 통째로 막을 뻔했다.
#
# 홀은 파티션(y 11.398)에서 끝난다. 주방은 **바운즈 밖**이라 따로 막을 필요가
# 없다 — 블로커를 늘리는 것보다 바운즈를 좁히는 쪽이 항상 낫다.
BOUNDS = ('LL_Nav_Hall', (-0.10, 0.00, -0.30, 6.10, 11.40, 2.60))

# 못 가는 곳 — 블렌더 좌표
BLOCKERS = [
    # 바 테이블 밑 (Bar_Top x 0.410~2.600 / y 0.100~0.550, 상판 z 0.68)
    # 상판 아래가 뚫려 있어 콜리전만으로는 통과 가능하다
    ('LL_NavBlock_Bar', (0.35, 0.05, -0.30, 2.65, 0.60, 2.60)),
]

PREFIX = 'LL_Nav'


def spawn_box(eas, cls, label, blender_box):
    center, size = ue_box(*blender_box)
    a = eas.spawn_actor_from_class(cls, center, unreal.Rotator(0, 0, 0))
    a.set_actor_label(label)
    # ⚠ 브러시는 스케일로 키운다 — 기본 큐브가 200 유닛이다
    a.set_actor_scale3d(unreal.Vector(size.x / BRUSH, size.y / BRUSH,
                                      size.z / BRUSH))
    return a, center, size


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)

    # 다시 돌릴 때를 위해 이전 것을 지운다
    old = [a for a in eas.get_all_level_actors()
           if a.get_actor_label().startswith(PREFIX)]
    for a in old:
        eas.destroy_actor(a)
    unreal.log_warning('=== NavMesh ===')
    unreal.log_warning('  기존 %d개 제거' % len(old))

    label, box = BOUNDS
    a, c, s = spawn_box(eas, unreal.NavMeshBoundsVolume, label, box)
    unreal.log_warning('  %s  중심 (%.0f %.0f %.0f) 크기 (%.0f %.0f %.0f)'
                       % (label, c.x, c.y, c.z, s.x, s.y, s.z))

    for lb, bx in BLOCKERS:
        a2, c2, s2 = spawn_box(eas, unreal.NavModifierVolume, lb, bx)
        # ⚠ `AreaClass` 를 안 주면 **막지 않는다.** NavArea_Null 이 「못 감」이다
        try:
            a2.set_editor_property('area_class', unreal.NavArea_Null)
        except Exception as e:
            unreal.log_warning('     ⚠ %s area_class 설정 실패: %s' % (lb, e))
        unreal.log_warning('  %s  중심 (%.0f %.0f %.0f) 크기 (%.0f %.0f %.0f)'
                           % (lb, c2.x, c2.y, c2.z, s2.x, s2.y, s2.z))

    # ---- RecastNavMesh — 에이전트 크기를 마네킹에 맞춘다 ----
    # 마네킹 캡슐: 반지름 34 / 반높이 88 (템플릿 기본)
    # ⚠ **`RecastNavMesh` 는 직접 스폰할 수 없다.** `spawn_actor_from_class` 가
    #   조용히 **None** 을 돌려준다 — 내비 시스템이 바운즈를 보고 자기가 만드는
    #   액터이기 때문이다. 커맨드릿에는 내비 빌드가 안 도니 여기서는 안 생긴다.
    #   에디터를 한 번 열면 생긴다. 생긴 뒤 이 스크립트를 다시 돌리면 값이 잡힌다.
    recast = [a for a in eas.get_all_level_actors()
              if isinstance(a, unreal.RecastNavMesh)]
    if not recast:
        unreal.log_warning('  RecastNavMesh 없음 — **에디터를 한 번 열면**'
                           ' 내비 시스템이 만든다.')
        unreal.log_warning('    그 뒤 이 스크립트를 다시 돌리면 에이전트 값이'
                           ' 마네킹(반지름 34 / 높이 176 / 단차 45)에 맞춰진다.')
    # ⚠⚠ **여기서 쓴 값은 세션이 끝나면 사라진다.** 확인한 사실:
    #     · 같은 세션에서 되읽으면 34 / 176 으로 잡힌다
    #     · **레벨을 저장하고 새 세션에서 다시 읽으면 35 / 144 로 돌아간다**
    #   `RecastNavMesh` 는 내비 시스템이 **프로젝트 설정의 `SupportedAgents`
    #   에서 만드는** 액터라, 액터에 쓴 값이 재생성 때 덮인다.
    #
    #   처음에 `except: pass` 로 감싸 놓고 「설정됐다」고 보고했는데 전부
    #   기본값이었다. **성공했다고 말하지 말고 되읽어 대조한다.**
    #
    #   진짜로 바꾸려면 **Project Settings > Navigation System >
    #   Supported Agents** (= `DefaultEngine.ini` 의
    #   `[/Script/NavigationSystem.NavigationSystemV1]` `+SupportedAgents=...`).
    #   ⚠ 그 항목은 문법이 틀리면 **내비가 통째로 죽는데** 커맨드릿에서는
    #     내비를 굽지 않아 검증할 수가 없다. 그래서 여기서는 손대지 않는다.
    #
    #   기본값으로도 이 매장은 돈다 — 마네킹 캡슐이 반지름 34 / 높이 176 이라
    #   반지름 35 는 1cm 차이고, 높이 144 가 문제되려면 **1.44m 아래를 지나야
    #   하는데** 테이블(0.70m)·카운터는 어차피 막힌다. 문 단차 140mm 도
    #   기본 단차 35cm 아래다.
    WANT = [('agent_radius', 34.0), ('agent_height', 176.0),
            ('agent_max_slope', 44.0)]
    for r in recast:
        for prop, val in WANT:
            try:
                r.set_editor_property(prop, val)
            except Exception as e:
                unreal.log_warning('     %s 설정 실패: %s' % (prop, e))
        unreal.log_warning('  %s' % r.get_actor_label())
        stuck = []
        for prop, val in WANT:
            try:
                got = float(r.get_editor_property(prop))
            except Exception:
                got = float('nan')
            ok = abs(got - val) < 0.01
            unreal.log_warning('     %-18s 원함 %-7.1f 실제 %-7.1f %s'
                               % (prop, val, got, 'OK' if ok else '**안 먹음**'))
            if not ok:
                stuck.append(prop)
        if stuck:
            unreal.log_warning('     -> %s 설정 자체가 실패했다' % stuck)
        unreal.log_warning('     ⚠ **이 값은 이번 세션에서만 유효하다.**'
                           ' 새로 열면 프로젝트 기본값(35 / 144)으로 돌아간다 —')
        unreal.log_warning('       바꾸려면 Project Settings > Navigation System'
                           ' > Supported Agents. 기본값으로도 이 매장은 돈다')

    les.save_current_level()
    unreal.log_warning('=== 완료 — 레벨 저장됨 ===')
    unreal.log_warning('  ⚠ **에디터에서 `P` 로 확인할 것.** 홀 통로만 초록이고')
    unreal.log_warning('    주방·카운터 안쪽·테이블 위가 비어 있어야 한다.')
    unreal.log_warning('  ⚠ 커맨드릿은 내비를 굽지 않는다 — 에디터를 열면 빌드된다.')


main()
