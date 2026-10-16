"""로코모션 애님의 **루트모션 플래그를 끈다.**

    에디터를 **끄고** 돌린다.

## ⚠⚠ 무슨 일이 있었나

`ll_walk_retarget.py` 가 만든 `Walk_Fwd_Manny` 가 소스(`A_WalkFwd_Loop`)의
플래그를 **그대로 물려받았다.**

```
A_WalkFwd_Loop   EnableRootMotion=True · ForceRootLock=True   (마네퀸: root 본에 전진이 실려 있다)
Walk_Fwd_Manny   EnableRootMotion=True · ForceRootLock=True   ← 복사됐다
```

그런데 `ABP_Customer` 의 루트모션 모드가 **`RootMotionFromMontagesOnly`** 다.
§9-2 에서 로코모션을 몽타주로 바꾸면서 이 둘이 맞물렸다 —

```
AM_Loco_Walk 에 루트모션이 있다
  -> AnimInstance 가 그것을 뽑는다
  -> CharacterMovement 가 **속도를 루트모션으로 덮어쓴다** (MaxWalkSpeed 무시)
  -> 그런데 그 애님은 scale_horizontal=0 으로 **제자리**로 구웠다 (전진 0.00cm)
  -> 루트모션이 0 이라 **거의 안 움직인다**
```

「350 으로 안 가고 느리게 간다」의 정체가 이것이다.

⚠ 회전도 같이 망가진다. 루트모션이 이동을 쥐고 있으면 `SetActorRotation` 으로
  넣은 자세가 다음 프레임에 밀린다.

## ⚠ `ForceRootLock` 도 끈다

Mixamo 는 **`Hips` 가 루트 본**이다. 루트를 레퍼런스 포즈에 고정하면 걷기의
**상하 바운스와 골반 높이가 통째로 죽는다.** 마네퀸처럼 `root` 본이 따로 있는
스켈레톤에서만 뜻이 있는 설정이다.

→ 손님 애님은 전부 **둘 다 False** 여야 한다. 실측으로 다른 애님(`Idle` ·
  `Sitting__1_` · `Reaching_Out` · `Eating_*` · `SitIdle_Male`)은 전부 그렇다.
  `Walk_Fwd_Manny` 만 튀어 있었다.

## ⚠⚠ 왜 검산에서 안 걸렸나

`ll_walk_retarget.py` 의 되읽기가 **스켈레톤 · 움직임 · 골반 Z · yaw · 전진량**은
봤는데 **플래그는 안 봤다.** 전진량이 0 인 것을 「제자리로 잘 구워졌다」로 읽었고,
그게 루트모션과 맞물려 반대 효과를 낸다는 것을 못 봤다. 이 스크립트가 그 검사를
추가한다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

DIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'

# 손님 애님은 전부 제자리 재생이다 — 전진은 CharacterMovement 가 한다.
ALL = ('Walk_Fwd_Manny', 'Idle', 'Walking', 'Walking_While_Texting',
       'Reaching_Out', 'Sitting_Talking', 'SitIdle_Male',
       'Eating_a_Burger', 'Eating_Fries', 'Eating_a_Drumstick', 'Eating_Frantically')

changed = []
L('=== 루트모션 플래그 정리 ===')
for n in ALL:
    a = EAL.load_asset('%s/%s' % (DIR, n))
    if not a:
        continue
    rm = a.get_editor_property('enable_root_motion')
    lk = a.get_editor_property('force_root_lock')
    if not rm and not lk:
        L('  %-22s 이미 False/False' % n)
        continue
    a.set_editor_property('enable_root_motion', False)
    a.set_editor_property('force_root_lock', False)
    L('  %-22s %s/%s -> %s/%s  ** 고침 **'
      % (n, rm, lk, a.get_editor_property('enable_root_motion'),
         a.get_editor_property('force_root_lock')))
    changed.append(a)

if not changed:
    L('')
    L('  고칠 것이 없다')
else:
    pkgs = list({c.get_outermost() for c in changed})
    ok = unreal.EditorLoadingAndSavingUtils.save_packages(pkgs, only_dirty=False)
    L('')
    L('  저장 %d개 -> %s' % (len(pkgs), ok))
    if not ok:
        E('  ⚠⚠ 안 써졌다 — **에디터가 켜져 있으면 전부 False 다** (§6). 닫고 다시.')

# ---- 몽타주는 시퀀스를 참조만 하므로 따로 고칠 것이 없다 ----
L('')
L('=== 확인: 로코모션 몽타주가 물고 있는 시퀀스 ===')
for n in ('AM_Loco_Walk', 'AM_Loco_Idle', 'AM_Loco_Sit'):
    m = EAL.load_asset('/Game/BurgerHouse/Anims/Montages/%s' % n)
    if not m:
        continue
    seg = (m.get_editor_property('slot_anim_tracks')[0]
           .get_editor_property('anim_track').get_editor_property('anim_segments')[0])
    s = seg.get_editor_property('anim_reference')
    L('  %-16s <- %-18s enable_root_motion=%s'
      % (n, s.get_name(), s.get_editor_property('enable_root_motion')))

L('')
L('⚠ 몽타주의 루트모션 여부는 **물고 있는 시퀀스**가 정한다. 시퀀스를 끄면')
L('  몽타주도 같이 꺼진다 — 몽타주를 따로 고칠 필요는 없다.')
L('')
L('=== 검산은 새 프로세스에서 ===')
