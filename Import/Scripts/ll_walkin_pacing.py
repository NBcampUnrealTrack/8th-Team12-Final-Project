"""워크인 생성 리듬 · 키오스크 도착 정밀도 · 알림음 볼륨을 손본다.

    에디터를 **끄고** 돌린다.

## 넷을 한꺼번에

| | 전 | 후 |
|---|---|---|
| `FirstSpawnDelay` (+지터) | 없음 (`BeginPlay` 가 즉시 스폰) | **60 ±30초** |
| `SpawnBurstWeights` | `{0.8, 0.2}` = 1명 80% · 2명 20% | **`{1.0}`** = 항상 1명 |
| `K_Act ▸ FaceYawOffset` | 15 | **0** (회전 자체는 유지) |
| `K_Move ▸ bReachTestIncludesAgentRadius` | true | **false** |
| `K_Order ▸ VolumeMultiplier` | 1.0 | **0.2** |

## ⚠ 15도만 뺀다 — `FaceSlotKind` 는 **남긴다**

`FaceSlotKind=Kiosk` 는 「도착하면 키오스크를 본다」이고, 그건 §9 가 미해결로
적어 뒀던 버그(**손님이 키오스크를 등지고 선다**)를 고친 것이다. 15도만 0 으로
되돌린다.

## 도착 위치 정밀도 — `bReachTestIncludesAgentRadius` 가 범인이다

```
AcceptableRadius 5   +   에이전트 반지름 34   =   최대 39cm 떨어져서 멈춘다
```

`bReachTestIncludesAgentRadius=true` 면 **캡슐 가장자리**가 목표에 닿으면 도착으로
친다. 끄면 **캡슐 중심**이 5cm 안에 들어와야 하므로 34cm가 줄어든다.

⚠ 대신 캡슐이 키오스크 메시에 더 가까워진다. 파고들면 `AcceptableRadius` 를
  10~15 로 올리는 것이 다음 수순이다 (그래도 예전 39cm 보다 정확하다).

⚠ 이보다 더 정확히 하려면 도착 시 **좌표를 스냅**해야 한다 (`SitAt` 이 좌석에서
  하는 것처럼). 그건 순간이동이라 눈에 띄므로 여기서는 안 했다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/BP/ST_Customer'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'

FIRST_DELAY, FIRST_JITTER = 60.0, 30.0
ACCEPT_RADIUS = 5.0
VOLUME = 0.2

# ───────────────────────── ① · ② 스포너 (레벨 액터)
L('=== ①② 스포너 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
pkgs = []
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    L('  %s' % sp.get_name())
    for p, v in (('first_spawn_delay', FIRST_DELAY),
                 ('first_spawn_delay_jitter', FIRST_JITTER)):
        before = sp.get_editor_property(p)
        sp.set_editor_property(p, v)
        L('    %-28s %7.1f -> %7.1f' % (p, before, sp.get_editor_property(p)))

    # ⚠ 가중치 배열은 **정규화해서** 쓴다. 원소가 하나면 그것이 100% 다.
    before = [float(x) for x in sp.get_editor_property('spawn_burst_weights')]
    arr = unreal.Array(float)
    arr.append(1.0)
    sp.set_editor_property('spawn_burst_weights', arr)
    L('    %-28s %s -> %s  (항상 1명)'
      % ('spawn_burst_weights', before,
         [float(x) for x in sp.get_editor_property('spawn_burst_weights')]))
    L('    (참고) SpawnInterval %.0f ±%.0f'
      % (sp.get_editor_property('spawn_interval'),
         sp.get_editor_property('spawn_interval_jitter')))
    pkgs.append(sp.get_outermost())

ok = unreal.EditorLoadingAndSavingUtils.save_packages(list(set(pkgs)), only_dirty=False)
L('  레벨 저장 -> %s' % ok)
if not ok:
    E('  ⚠⚠ 안 써졌다 — **에디터가 켜져 있으면 전부 False** 다 (§6)')

# ───────────────────────── ③ 15도 제거 · 도착 정밀도 · 볼륨 (트리)
L('')
L('=== ③ StateTree ===')


def setp(state, task, prop, val):
    r = S.set_task_property_value(ST, state, task, prop, val)
    L('  %-24s %-30s = %-8s -> %s' % (state.split('/')[-1], prop, val, r))
    return r


setp('Root/Kiosk/K_Act', 'BurgerHouseActionTask', 'FaceYawOffset', '0.0')

# ⚠ 이 둘이 같이 가야 한다. Radius 만 줄이고 AgentRadius 를 켜 두면 34cm 가 그대로 남는다.
setp('Root/Kiosk/K_Move', 'StateTreeMoveToTask', 'bReachTestIncludesAgentRadius', 'false')
setp('Root/Kiosk/K_Move', 'StateTreeMoveToTask', 'AcceptableRadius', str(ACCEPT_RADIUS))

setp('Root/Kiosk/K_Order', 'BurgerHousePlaySoundTask', 'VolumeMultiplier', str(VOLUME))

# ⚠ **저장을 컴파일보다 먼저.** 그리고 컴파일이 만든 런타임 데이터도 써야 하므로 한 번 더.
pkg = EAL.load_asset(ST).get_outermost()
L('')
L('  [컴파일 전] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
L('  compile -> %s' % S.compile_state_tree(ST))
L('  [컴파일 후] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

L('')
L('=== 검산은 새 프로세스에서 ===')
