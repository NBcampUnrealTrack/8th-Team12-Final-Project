"""`L_BurgerHouse` 의 워크인 스폰 주기를 바꾼다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_spawn_interval.py" -unattended -nopause -nosplash

## 무엇을 바꾸나

| | 전 | 후 |
|---|---|---|
| `SpawnInterval` | 40s | **240s** |
| `SpawnIntervalJitter` | ±5s | **±30s** |

지터를 같이 올리는 이유 — 헤더가 이렇게 적어 놨다:

> ⚠ **고정 주기는 사람이 아니라 메트로놈으로 보인다.**

40초에 ±5 는 **±12.5%** 였다. 240초에 ±5 를 그대로 두면 **±2%** 라 사실상
고정 주기가 되어 위 경고에 정면으로 걸린다. 같은 비율을 유지해 ±30 으로 둔다
(210~270초). ±5 를 고집하려면 `JITTER` 만 5 로 바꿔 다시 돌리면 된다.

## ⚠⚠ 레벨 액터 프로퍼티는 **조용히 안 써진다** (§6)

`set_editor_property` 만으로는 레벨 패키지가 dirty 로 안 잡힌다. 그 상태로
저장하면 `True` 가 돌아오고 같은 프로세스에서 되읽어도 값이 맞게 나오는데
**파일은 안 써진다.** `save_packages(..., only_dirty=False)` 로 dirty 플래그를
무시하게 해야 한다. 그리고 검산은 **반드시 새 프로세스**에서.

⚠ `L_BurgerHouse` 는 OFPA(`__ExternalActors__`)를 안 쓴다 — 확인했다. 그래서
  액터가 월드 패키지 안에 있고 월드만 저장하면 된다. OFPA 레벨이면 액터가
  **자기 패키지**를 따로 가지므로 이 코드로는 안 써진다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
INTERVAL = 240.0
JITTER = 30.0

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
spawners = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner)
if not spawners:
    E('CustomerSpawner 를 못 찾았다')
    raise SystemExit

pkgs = []
for sp in spawners:
    L('=== %s ===' % sp.get_name())
    b_int = sp.get_editor_property('spawn_interval')
    b_jit = sp.get_editor_property('spawn_interval_jitter')
    sp.set_editor_property('spawn_interval', INTERVAL)
    sp.set_editor_property('spawn_interval_jitter', JITTER)
    L('  SpawnInterval        %7.1f -> %7.1f' % (b_int, sp.get_editor_property('spawn_interval')))
    L('  SpawnIntervalJitter  %7.1f -> %7.1f' % (b_jit, sp.get_editor_property('spawn_interval_jitter')))

    # 바꾸지 않지만 결과를 좌우하는 값들 — 같이 찍어 둔다
    for p in ('fixed_desired_alive', 'hard_max_alive', 'spawn_burst_weights',
              'prefill_on_begin_play'):
        L('  %-22s %s' % (p, sp.get_editor_property(p)))

    pkgs.append(sp.get_outermost())

# ---- 저장 ----
#
# ⚠ `save_current_level()` 은 `True` 를 주고도 안 쓴다 (§6). only_dirty=False 로.
ok = unreal.EditorLoadingAndSavingUtils.save_packages(list(set(pkgs)), only_dirty=False)
L('')
L('저장 save_packages(only_dirty=False) -> %s' % ok)
if not ok:
    E('⚠⚠ 레벨이 **디스크에 안 써졌다.** 아래 되읽기는 메모리라 의미가 없다.')

# ---- 결과 예측 ----
sp = spawners[0]
w = list(sp.get_editor_property('spawn_burst_weights'))
tot = sum(w) or 1.0
avg = sum((i + 1) * x for i, x in enumerate(w)) / tot
desired = sp.get_editor_property('fixed_desired_alive')

L('')
L('=== 이 값이 뜻하는 것 ===')
L('  한 번에 오는 인원 기대값 %.2f명 (가중치 %s)' % (avg, w))
L('  -> 시간당 %.1f명 · 시간당 도착 %.1f회'
  % (3600.0 / INTERVAL * avg, 3600.0 / INTERVAL))
L('  정원 %d명 중 프리필 5명은 **영구 장식**이라 워크인 자리는 %d개'
  % (desired, desired - 5))
L('  -> 빈 자리 %d개를 채우는 데 평균 %.1f분 (도착 %.1f회)'
  % (desired - 5, (desired - 5) / avg * INTERVAL / 60.0, (desired - 5) / avg))
L('  ⚠ 손님이 나간 자리도 최대 %.0f분 뒤에야 다시 찬다.' % (INTERVAL / 60.0))
L('')
L('=== 검산은 ll_spawn_interval_verify.py 로 **새 프로세스**에서 ===')
