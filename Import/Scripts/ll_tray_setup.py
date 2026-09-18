"""트레이 세트 · 캐리 애님 · 주문 알림음을 배선한다.

    에디터를 **끄고** 돌린다.

## 하는 것

| | |
|---|---|
| ① 캐리 몽타주 | `Carrying` → `AM_Loco_CarryWalk` · `Idle` → `AM_Loco_CarryIdle` (LocoSlot) |
| ② 트레이 구성 | `BP_Customer` 의 `Tray` 에 메시를 꽂고 버거·감자·영수증을 얹는다 |
| ③ 양손 들기 | `TraySocket` 을 손이 아니라 **몸통(`spine2`)** 으로 |
| ④ 알림음 액터 | 주문이 나왔을 때 울릴 `AmbientSound` 추가 + 태그 |
| ⑤ 트리 태스크 | `Pickup` 에 `Play Sound At Tagged Actor` |

## ⚠⚠ 양손으로 들려면 **애님**이 그래야 한다

트레이를 몸통에 붙이는 것만으로는 손이 안 따라온다 — 손 위치는 애님이 정한다.
Mixamo 세트의 `Carrying` 이 두 손을 앞으로 모은 동작이라 그것을 쓴다.
`ACustomerCharacter::UpdateLocomotionMontage` 가 `bHasTray` 를 보고 갈아 끼운다.

## ⚠ 스케일은 실측에서 나왔다

```
SM_BurgerBox   임포트 2.5 × 2.6 × 1.3 cm   -> ×5.0  = 12.5 × 13 × 6.5
SM_Fries       임포트 24.5 × 11.6 × 24.0    -> ×0.4  = 9.8 × 4.6 × 9.6
Receipt        Plane 100 × 100              -> (0.08, 0.15, 1) = 8 × 15
```

⚠ 트레이 위 **배치 오프셋은 추정**이다. 트레이 메시를 안 보고 잡은 값이라
  에디터에서 눈으로 맞출 것. 전부 `BP_Customer` 에서 편집 가능하다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

BP = '/Game/BurgerHouse/AI/BP_Customer'
ST = '/Game/BurgerHouse/AI/ST_Customer'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
MTG = '/Game/BurgerHouse/Anims/Montages'
ANIM = '/Game/BurgerHouse/Anims/Mixamo/Anims'
TAG = 'OrderReadyChime'

# ───────────────────────── ① 캐리 몽타주
L('=== ① 캐리 몽타주 ===')
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def make_montage(seq_name, mtg_name, slot='LocoSlot', blend=0.20):
    seq = EAL.load_asset('%s/%s' % (ANIM, seq_name))
    if not seq:
        E('  %s 시퀀스가 없다' % seq_name)
        return None
    path = '%s/%s' % (MTG, mtg_name)
    if EAL.does_asset_exist(path):
        m = EAL.load_asset(path)
    else:
        fac = unreal.AnimMontageFactory()
        fac.set_editor_property('target_skeleton', seq.get_editor_property('skeleton'))
        fac.set_editor_property('source_animation', seq)
        m = TOOLS.create_asset(mtg_name, MTG, unreal.AnimMontage, fac)
    if not m:
        E('  %s 생성 실패' % mtg_name)
        return None

    # ⚠ 배열 원소는 **복사본**이라 고친 뒤 되넣어야 한다 (`ll_montages.py` 와 같다).
    tr = m.get_editor_property('slot_anim_tracks')
    s0 = tr[0]
    s0.set_editor_property('slot_name', slot)
    tr[0] = s0
    m.set_editor_property('slot_anim_tracks', tr)
    for p in ('blend_in', 'blend_out'):
        b = m.get_editor_property(p)
        b.set_editor_property('blend_time', blend)
        m.set_editor_property(p, b)

    # ⚠ 루트모션 플래그를 **확인**한다. 켜져 있으면 캡슐 속도가 무시된다 (§9-2).
    rm = seq.get_editor_property('enable_root_motion')
    lk = seq.get_editor_property('force_root_lock')
    if rm or lk:
        seq.set_editor_property('enable_root_motion', False)
        seq.set_editor_property('force_root_lock', False)
        L('  %s 의 루트모션 플래그를 껐다 (%s/%s -> False/False)' % (seq_name, rm, lk))
    L('  %-20s <- %-16s %.2fs slot=%s' % (mtg_name, seq_name,
                                          m.get_editor_property('sequence_length'), slot))
    return m


carry_walk = make_montage('Carrying', 'AM_Loco_CarryWalk')
carry_idle = make_montage('Idle', 'AM_Loco_CarryIdle')

# ───────────────────────── ② · ③ BP_Customer
L('')
L('=== ②③ BP_Customer ===')
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())

# 액션 맵에 캐리 항목 추가 (기존 것은 건드리지 않는다)
actions = cdo.get_editor_property('actions')
for key, m in (('Loco_CarryWalk', carry_walk), ('Loco_CarryIdle', carry_idle)):
    if not m:
        continue
    variants = unreal.Array(unreal.CustomerActionVariant)
    v = unreal.CustomerActionVariant()
    v.set_editor_property('montage', m)
    v.set_editor_property('weight', 1.0)
    variants.append(v)
    ca = unreal.CustomerAction()
    ca.set_editor_property('variants', variants)
    actions[unreal.Name(key)] = ca
    L('  Actions[%s] <- %s' % (key, m.get_name()))
cdo.set_editor_property('actions', actions)

# ⚠⚠ 양손으로 들려면 손 소켓이 아니라 **몸통**에 붙여야 한다.
#    `RightHand` 에 붙이면 한 손에 대롱대롱 매달린다.
cdo.set_editor_property('tray_socket', 'spine2')
cdo.set_editor_property('tray_hand_offset', unreal.Transform(
    location=[18.0, 0.0, -8.0], rotation=[0.0, 90.0, 0.0], scale=[1.0, 1.0, 1.0]))
L('  TraySocket -> spine2 (몸통 중앙) · HandOffset 앞 18 · 아래 8')

# 트레이 본체 + 얹히는 것들
ITEMS = [
    ('tray', '/Game/BurgerHouse/Props/Tray', (0, 0, 0), (0, 0, 0), (1.0, 1.0, 1.0)),
    ('burger_box', '/Game/BurgerHouse/Props/Food/SM_BurgerBox',
     (-6.0, 0.0, 2.0), (0, 0, 0), (5.0, 5.0, 5.0)),
    ('fries', '/Game/BurgerHouse/Props/Food/SM_Fries',
     (7.0, -5.0, 2.0), (0, 0, 0), (0.4, 0.4, 0.4)),
    ('receipt', '/Engine/BasicShapes/Plane',
     (7.0, 7.0, 1.2), (0, 0, 0), (0.08, 0.15, 1.0)),
]
for prop, mesh_path, loc, rot, scl in ITEMS:
    comp = cdo.get_editor_property(prop)
    if not comp:
        E('  %s 컴포넌트가 없다 — 빌드 확인' % prop)
        continue
    mesh = EAL.load_asset(mesh_path)
    if not mesh:
        E('  %s 메시가 없다 (%s)' % (prop, mesh_path))
        continue
    comp.set_editor_property('static_mesh', mesh)
    if prop != 'tray':
        comp.set_editor_property('relative_location', unreal.Vector(*loc))
        comp.set_editor_property('relative_rotation', unreal.Rotator(*rot))
    comp.set_editor_property('relative_scale3d', unreal.Vector(*scl))
    b = mesh.get_bounds().box_extent
    L('  %-12s <- %-34s 최종 %.1f × %.1f × %.1f cm'
      % (prop, mesh.get_name(), b.x * 2 * scl[0], b.y * 2 * scl[1], b.z * 2 * scl[2]))

# ⚠ **저장이 컴파일보다 먼저**다 (§6).
ok = unreal.EditorLoadingAndSavingUtils.save_packages(
    [bp.get_outermost()] + [m.get_outermost() for m in (carry_walk, carry_idle) if m],
    only_dirty=False)
L('  저장 -> %s' % ok)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ───────────────────────── ④ 알림음 액터
L('')
L('=== ④ 주문 알림음 AmbientSound ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

existing = [a for a in unreal.GameplayStatics.get_all_actors_with_tag(world, unreal.Name(TAG))]
if existing:
    L('  태그 «%s» 액터가 이미 있다 — 다시 안 만든다' % TAG)
    snd_actor = existing[0]
else:
    # 픽업 카운터 위에 놓는다 — 주문이 나오는 자리다.
    pickups = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
               if str(s.get_editor_property('kind')) == 'Pickup']
    where = pickups[0].get_actor_location() if pickups else unreal.Vector(255, -1040, 0)
    where = unreal.Vector(where.x, where.y, where.z + 150.0)
    snd_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.AmbientSound, where, unreal.Rotator(0, 0, 0))
    snd_actor.set_actor_label('AmbientSound_OrderReady')
    L('  생성 %s @ %s' % (snd_actor.get_actor_label(), where))

tags = unreal.Array(unreal.Name)
tags.append(unreal.Name(TAG))
snd_actor.set_editor_property('tags', tags)

ac = snd_actor.get_editor_property('audio_component')
if ac:
    # ⚠ **Auto Activate 를 끈다.** 켜 두면 레벨이 열리자마자 계속 운다 (§9-3).
    ac.set_editor_property('auto_activate', False)
    chime = EAL.load_asset('/Game/BurgerHouse/Audio/SW_Chime_1')
    if chime:
        ac.set_editor_property('sound', chime)
    L('  Tags %s · AutoActivate %s · Sound %s'
      % ([str(t) for t in snd_actor.tags], ac.get_editor_property('auto_activate'),
         ac.get_editor_property('sound')))

ok = unreal.EditorLoadingAndSavingUtils.save_packages(
    [world.get_outer()], only_dirty=False)
L('  레벨 저장 -> %s' % ok)

# ───────────────────────── ⑤ 트리 태스크
L('')
L('=== ⑤ Pickup 에 알림음 태스크 ===')
STATE = 'Root/Pickup'
TASK = 'BurgerHousePlaySoundTask'
# ⚠ `Pickup` 은 `Wait` 가 `Order.Ready` 이벤트를 받아 전이하는 곳이다 —
#   「주문이 나왔을 때」가 정확히 이 순간이다.
if S.get_task_property_names(ST, STATE, TASK):
    L('  이미 있다 — 프로퍼티만 맞춘다')
else:
    L('  add_task -> %s' % S.add_task(ST, STATE, TASK))
for p, v in (('ActorTag', TAG), ('VolumeMultiplier', '0.2')):
    L('  set %-18s = %-16s -> %s' % (p, v, S.set_task_property_value(ST, STATE, TASK, p, v)))

pkg = EAL.load_asset(ST).get_outermost()
L('  [컴파일 전] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
L('  compile -> %s' % S.compile_state_tree(ST))
L('  [컴파일 후] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

L('')
L('=== 검산은 새 프로세스에서 ===')
