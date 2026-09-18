"""착석 체류 시간을 **1~3분**으로 맞춘다.

    에디터를 **끄고** 돌린다.

## 시간이 어떻게 만들어지나

`FBurgerHouseActionTask` —

```
총 재생 횟수 N = Plays + rand(0, RandomExtraRepeats)        (균등)
한 번 재생    = 몽타주 길이 + rand(0, RandomExtraSeconds)   (균등)
체류 = Σ (몽타주 + 덤)
```

`Sit-Eat` 몽타주 넷은 가중치가 같다 —

```
AM_Eating_Frantically  6.87s   <- 최소
AM_Eating_a_Burger     7.83s
AM_Eating_a_Drumstick  8.03s
AM_Eating_Fries        9.20s   <- 최대      평균 7.98s
```

## 값을 푸는 법

최악(가장 짧은 쪽)과 최선(가장 긴 쪽)을 **둘 다** 묶어야 한다.

```
최소 = Plays × 6.87                       ≥ 60
최대 = (Plays + Repeats) × (9.20 + 덤)    ≤ 180
```

- `Plays ≥ 60 / 6.87 = 8.73`  →  **9**
- `덤 = 3` 일 때 `(9 + R) × 12.20 ≤ 180`  →  `R ≤ 5.75`  →  **5**

```
최소   9 × 6.87          =  61.8초
최대  14 × (9.20 + 3.00) = 170.8초
기대값 11.5 × (7.98 + 1.5) ≈ 109초
```

⚠ 덤(`RandomExtraSeconds`)을 0 으로 두면 2분 내내 **쉬지 않고 먹는다.** 3초를
  남겨 한 입마다 잠깐 멈추게 했다 — 그 동안은 `LocoSlot` 의 앉은 몽타주가 비쳐
  자연스럽게 앉아 있다.

⚠ 「최소」는 **가장 짧은 몽타주가 9번 연속 뽑히고 덤이 전부 0** 인 경우다. 실제
  평균 재생 길이는 7.98 이라 보통은 72초 밑으로 안 내려간다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/AI/ST_Customer'

PLAYS = 9
REPEATS = 5
EXTRA = 3.0

# 워크인과 프리필(장식이 아닌) 둘 다 — 같은 「테이블에서 먹는」 상태다
STATES = (('Root/Seat/S_Eat/Eating', '워크인'),
          ('Root/P_Eating/PE_Seat/PE_Sit/PE_Eat', '프리필'))

fails = []

L('R === 착석 체류 1~3분 ===')
for st, who in STATES:
    for prop, val in (('Plays', str(PLAYS)),
                      ('RandomExtraRepeats', str(REPEATS)),
                      ('RandomExtraSeconds', '%.1f' % EXTRA)):
        r = S.set_task_property_value(ST, st, 'BurgerHouseActionTask', prop, val)
        if not r:
            fails.append('%s.%s' % (st, prop))
    d = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
         for p in (S.get_task_property_names(ST, st, 'BurgerHouseActionTask') or [])}
    L('R   %-8s %-38s Action=%-9s Plays=%s Repeats=%s Extra=%s'
      % (who, st, d.get('ActionName'), d.get('Plays'),
         d.get('RandomExtraRepeats'), d.get('RandomExtraSeconds')))

# ───────────────────────── 계산 결과를 **몽타주 실측 길이로** 되짚는다
L('R')
L('R === 되짚기 ===')
cdo = unreal.get_default_object(EAL.load_asset('/Game/BurgerHouse/AI/BP_Customer').generated_class())
for key, who in (('Sit-Eat', '워크인'), ('Sit_Eat', '프리필')):
    act = cdo.get_editor_property('actions').get(key)
    if not act:
        E('R   Actions 에 %s 가 없다' % key)
        fails.append(key)
        continue
    lens = [v.get_editor_property('montage').get_play_length()
            for v in act.get_editor_property('variants')
            if v.get_editor_property('montage')]
    lo, hi = min(lens), max(lens)
    avg = sum(lens) / len(lens)
    t_min = PLAYS * lo
    t_max = (PLAYS + REPEATS) * (hi + EXTRA)
    t_avg = (PLAYS + REPEATS / 2.0) * (avg + EXTRA / 2.0)
    L('R   %-8s %s  몽타주 %.2f~%.2f (평균 %.2f)' % (who, key, lo, hi, avg))
    L('R       최소 %.1f초 · 최대 %.1f초 · 기대값 %.1f초  %s'
      % (t_min, t_max, t_avg,
         'OK' if (t_min >= 60.0 and t_max <= 180.0) else '<-- ⚠ 1~3분 밖'))
    if t_min < 60.0 or t_max > 180.0:
        fails.append('%s 범위' % key)

pkg = EAL.load_asset(ST).get_outermost()
L('R')
L('R   save %s / compile %s / save %s' % (
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False),
    S.compile_state_tree(ST).get_editor_property('success'),
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)))

L('R')
if fails:
    E('R === 실패: %s ===' % fails)
else:
    L('R === 완료 ===')
