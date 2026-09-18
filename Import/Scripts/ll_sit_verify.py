"""프리필 손님의 앉은 몸짓 풀(`Sit_Idle`)을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리를 보는 것이라 의미가 없다 (§6).

보는 것 —
  ① `BP_Customer > Actions["Sit_Idle"]` 구성 · 가중치
  ② 풀의 **모든** 몽타주: 슬롯 · 블렌드 · 스켈레톤
  ③ ⚠⚠ 풀의 **모든** 동작이 앉은 높이인가 (§7 의 「일어선다」 사고 방지)
  ④ 루프 이음매
  ⑤ 프리필이 정말 전부 `Eating`(= `Sit_Idle`) 인가
  ⑥ 트리가 쓰는 `Sit_Eat` 은 안 건드려졌는가

## ③ 이 검사가 이 스크립트의 핵심이다

이름에 `Sitting` 이 들어가도 **서 있는 동작**인 것이 섞여 있다. 그걸 전신
슬롯으로 틀면 앉은 손님이 일어선다. 에러가 안 나고 화면에서만 틀린다.
그래서 풀에 들어간 것을 **전부** 재서 기준(먹는 애님 57.4~57.6)과 견준다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP_PATH = '/Game/BurgerHouse/BP/BP_Customer'

# 기준 — 화면에서 확인된 먹는 애님의 골반 높이
SIT_BASE = 57.5
SIT_TOL = 2.0          # ±2cm 를 넘으면 의자에 뜨거나 파묻힌다

EXPECTED_SIT_IDLE = {
    'AM_Eating_a_Burger': 1.0, 'AM_Eating_Fries': 1.0,
    'AM_Eating_a_Drumstick': 1.0, 'AM_Eating_Frantically': 0.6,
    'AM_Sitting_Talking': 0.8, 'AM_SitIdle_Male': 0.5,
}

BONES = ('Hips', 'Spine', 'Spine1', 'Neck', 'Head', 'LeftArm', 'LeftForeArm',
         'RightArm', 'RightForeArm', 'LeftUpLeg', 'LeftLeg', 'RightUpLeg', 'RightLeg')

fails = []


def source_seq(m):
    """몽타주가 물고 있는 시퀀스. 프로퍼티 이름이 판본마다 달라 하나씩 시도한다."""
    try:
        track = m.get_editor_property('slot_anim_tracks')[0].get_editor_property('anim_track')
        segs = track.get_editor_property('anim_segments')
    except Exception as ex:
        E('    세그먼트를 못 읽음: %s' % str(ex)[:70])
        return None
    for seg in segs:
        for p in ('anim_reference', 'anim_sequence', 'animation'):
            try:
                v = seg.get_editor_property(p)
                if v:
                    return v
            except Exception:
                continue
    return None


def rot(a, b, t):
    return unreal.AnimationLibrary.get_bone_pose_for_time(a, b, t, False).rotation.rotator()


def measure(seq):
    dur = seq.get_editor_property('sequence_length')
    z = unreal.AnimationLibrary.get_bone_pose_for_time(
        seq, 'Hips', 0.0, False).translation.z
    seam = 0.0
    for b in BONES:
        try:
            r0, r1 = rot(seq, b, 0.0), rot(seq, b, dur - 1e-4)
        except Exception:
            continue
        seam += abs(r1.pitch - r0.pitch) + abs(r1.yaw - r0.yaw) + abs(r1.roll - r0.roll)
    return dur, z, seam


# ---- ① Actions ----
L('=== ① BP_Customer > Actions ===')
cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP_PATH))
actions = cdo.get_editor_property('actions')
got_sit_idle = {}
for key in actions.keys():
    variants = actions[key].get_editor_property('variants')
    names = []
    for v in variants:
        m = v.get_editor_property('montage')
        w = v.get_editor_property('weight')
        nm = m.get_name() if m else '(없음)'
        names.append('%s(%.1f)' % (nm, w))
        if str(key) == 'Sit_Idle':
            got_sit_idle[nm] = w
    L('  %-18s %s' % (key, ' · '.join(names)))

if set(got_sit_idle) != set(EXPECTED_SIT_IDLE):
    fails.append('Sit_Idle 구성이 다르다: %s' % sorted(got_sit_idle))
else:
    for n, w in EXPECTED_SIT_IDLE.items():
        if abs(got_sit_idle[n] - w) > 0.01:
            fails.append('%s 가중치가 %.2f 다 (기대 %.2f)' % (n, got_sit_idle[n], w))
    total = sum(got_sit_idle.values())
    eat = sum(w for n, w in got_sit_idle.items() if 'Eating' in n)
    L('')
    L('  가중치 합 %.1f · 먹는 것 %.1f (%.0f%%) — 버거집으로 읽히는 비율'
      % (total, eat, eat / total * 100))

# ⑥ 트리 쪽은 그대로여야 한다
sit_eat = [v.get_editor_property('montage').get_name()
           for v in actions[unreal.Name('Sit_Eat')].get_editor_property('variants')]
if len(sit_eat) != 4 or not all('Eating' in n for n in sit_eat):
    fails.append('Sit_Eat 이 바뀌었다: %s' % sit_eat)
else:
    L('  -> Sit_Eat 은 먹는 몽타주 4종 그대로 (걸어 들어온 손님용)')

# ---- ②③④ 풀 전체 ----
L('')
L('=== ②③④ Sit_Idle 풀 전체 ===')
L('  %-24s %7s %7s %-6s %8s %s' % ('몽타주', '길이', '골반Z', '판정', '이음매', '슬롯/블렌드'))
for nm in sorted(got_sit_idle):
    m = EAL.load_asset('/Game/BurgerHouse/Anims/Montages/%s' % nm)
    if not m:
        fails.append('%s 를 못 읽는다' % nm)
        continue
    slot = str(m.get_editor_property('slot_anim_tracks')[0]
               .get_editor_property('slot_name'))
    bi = m.get_editor_property('blend_in').get_editor_property('blend_time')
    sk = m.get_editor_property('skeleton')

    seq = source_seq(m)
    if not seq:
        fails.append('%s 의 소스 시퀀스를 못 찾는다' % nm)
        continue
    dur, z, seam = measure(seq)

    off = z - SIT_BASE
    ok = abs(off) <= SIT_TOL
    verdict = '앉음' if ok else ('%+.1fcm' % off)
    L('  %-24s %6.2fs %7.1f %-6s %7.1f도 %s/%.2f'
      % (nm, dur, z, verdict, seam, slot, bi))

    if not ok:
        fails.append('%s 골반 Z %.1f — 기준 %.1f 에서 %+.1fcm (의자에 뜨거나 파묻힌다)'
                     % (nm, z, SIT_BASE, off))
    if slot != 'DefaultSlot':
        fails.append('%s 슬롯이 %s 다 — DefaultSlot 이라야 ABP 의 Slot 노드를 탄다'
                     % (nm, slot))
    if abs(bi - 0.35) > 0.01:
        fails.append('%s BlendIn 이 %.2f 다 — 크로스페이드 길이를 정하는 값이다' % (nm, bi))
    if not sk or sk.get_name() != 'Hip_Hop_Dancing_Skeleton':
        fails.append('%s 스켈레톤이 손님 것이 아니다' % nm)

L('')
L('  ⚠ 이음매는 DecorationLoopCrossfade 0.35 가 덮는다 — 커도 못 쓰는 건 아니다.')
L('    (재생이 끝나기 0.35초 전에 다음 것을 겹쳐 걸어 기본 포즈를 안 거친다)')

# ---- ⑤ 프리필 역할 ----
L('')
L('=== ⑤ 프리필이 실제로 낳는 역할 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
deco = cdo.get_editor_property('decoration_actions')
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    pl = sp.get_editor_property('prefill_placements')
    roles, parties = {}, {}
    for p in pl:
        r = str(p.get_editor_property('role'))
        roles[r] = roles.get(r, 0) + 1
        pi = p.get_editor_property('party_index')
        parties[pi] = parties.get(pi, 0) + 1
    L('  %s · 프리필 %d명 · 역할 %s · 테이블(party) %s'
      % (sp.get_name(), len(pl), roles, parties))
    for r in roles:
        act = deco.get(unreal.Name(r))
        L('    %-10s -> %s' % (r, act))
        if str(act) != 'Sit_Idle':
            fails.append('프리필 역할 %s 는 %s 를 쓴다 — 이 풀 밖이다' % (r, act))

L('')
L('  변형 %d개를 손님 %d명이 좌석 해시로 나눠 받는다 (DecorationSeed=%s).'
  % (len(got_sit_idle), sum(1 for _ in pl), cdo.get_editor_property('decoration_seed')))
L('  ⚠ 실제 배분은 FName 해시라 여기서 못 흉내 낸다 — PIE 로그의')
L('    「장식 몸짓 ... 변형 N」 을 보고, 겹치면 DecorationSeed 만 바꿀 것.')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 전부 통과 ===')
    L('⚠ 자세는 **눈으로** 봐야 한다. 어긋나면 RTG_MannySit 을 Edit Pose 로 맞출 것.')
