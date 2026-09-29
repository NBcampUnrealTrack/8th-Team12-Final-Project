"""2단계 검산 — 로코모션 몽타주를 **새 프로세스**에서 본다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).

보는 것 —
  ① `AM_Loco_*` 3개: 슬롯 `LocoSlot` · 블렌드 **0.20** · 스켈레톤 · 섹션
  ② 기존 몽타주가 0.35 그대로인가 (`set_blend` 에 슬롯 인자를 붙인 회귀 검사)
  ③ `BP_Customer > Actions` 에 `Loco_*` 3개가 붙었고 기존 6개가 그대로인가
  ④ 3단계 C++ 가 쓸 섹션 이름
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

MTG_DIR = '/Game/BurgerHouse/Anims/Montages'
BP_PATH = '/Game/BurgerHouse/BP/BP_Customer'

LOCO = {'AM_Loco_Idle': ('Idle', 8.333), 'AM_Loco_Walk': ('Walk_Fwd_Manny', 1.067),
        'AM_Loco_Sit': ('SitIdle_Male', 2.700)}
OLD_SAMPLE = ('AM_Eating_a_Burger', 'AM_SitIdle_Male', 'AM_Idle')

fails = []


def info(name):
    m = EAL.load_asset('%s/%s' % (MTG_DIR, name))
    if not m:
        return None
    tr = m.get_editor_property('slot_anim_tracks')[0]
    seg = tr.get_editor_property('anim_track').get_editor_property('anim_segments')[0]
    return {
        'm': m,
        'slot': str(tr.get_editor_property('slot_name')),
        'in': m.get_editor_property('blend_in').get_editor_property('blend_time'),
        'out': m.get_editor_property('blend_out').get_editor_property('blend_time'),
        'len': m.get_editor_property('sequence_length'),
        'src': seg.get_editor_property('anim_reference'),
        'loopcnt': seg.get_editor_property('looping_count'),
        'skel': m.get_editor_property('skeleton'),
    }


L('=== ① 로코모션 몽타주 ===')
for name, (src_want, len_want) in LOCO.items():
    d = info(name)
    if not d:
        fails.append('%s 가 없다' % name)
        continue
    src = d['src'].get_name() if d['src'] else '(없음)'
    L('  %-16s slot=%-10s in=%.2f out=%.2f  %.3fs  <- %s (loopingCount=%s)'
      % (name, d['slot'], d['in'], d['out'], d['len'], src, d['loopcnt']))
    if d['slot'] != 'LocoSlot':
        fails.append('%s 슬롯이 %s 다 (기대 LocoSlot)' % (name, d['slot']))
    if abs(d['in'] - 0.20) > 0.01 or abs(d['out'] - 0.20) > 0.01:
        fails.append('%s 블렌드가 %.2f/%.2f 다 (기대 0.20 — 상태 기계 전이값)'
                     % (name, d['in'], d['out']))
    if src != src_want:
        fails.append('%s 가 %s 를 물고 있다 (기대 %s)' % (name, src, src_want))
    if abs(d['len'] - len_want) > 0.02:
        fails.append('%s 길이가 %.3f 다 (기대 %.3f)' % (name, d['len'], len_want))
    if not d['skel'] or d['skel'].get_name() != 'Hip_Hop_Dancing_Skeleton':
        fails.append('%s 스켈레톤이 손님 것이 아니다' % name)
    # ⚠ looping_count 는 **무한 루프 스위치가 아니다.** 0 이면 길이가 0 이 된다.
    if d['loopcnt'] != 1:
        fails.append('%s 의 looping_count 가 %s 다 — 1 이라야 한다 (무한 루프는 런타임 섹션으로)'
                     % (name, d['loopcnt']))

L('')
L('=== ② 기존 몽타주는 0.35 그대로여야 한다 (회귀 검사) ===')
for name in OLD_SAMPLE:
    d = info(name)
    if not d:
        continue
    ok = abs(d['in'] - 0.35) < 0.01
    L('  %-22s slot=%-12s in=%.2f  %s' % (name, d['slot'], d['in'], 'OK' if ok else '<-- ⚠'))
    if not ok:
        fails.append('%s 블렌드가 %.2f 다 (기대 0.35)' % (name, d['in']))

L('')
L('=== ③ BP_Customer > Actions ===')
cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP_PATH))
actions = cdo.get_editor_property('actions')
keys = sorted(str(k) for k in actions.keys())
L('  %d개: %s' % (len(keys), keys))
for k in ('Loco_Idle', 'Loco_Walk', 'Loco_Sit'):
    if k not in keys:
        fails.append('Actions 에 %s 가 없다' % k)
if len(actions[unreal.Name('Sit_Idle')].get_editor_property('variants')) != 6:
    fails.append('Sit_Idle 풀이 6개가 아니다 — 앞 작업이 깨졌다')

L('')
L('=== ④ 3단계 C++ 가 쓸 섹션 이름 ===')
for name in LOCO:
    m = EAL.load_asset('%s/%s' % (MTG_DIR, name))
    if not m:
        continue
    n = m.get_num_sections()
    names = [str(m.get_section_name(i)) for i in range(n)]
    L('  %-16s 섹션 %d개 %s' % (name, n, names))
    if n != 1:
        fails.append('%s 의 섹션이 %d개다 — 1개라야 자기 자신으로 루프시킬 수 있다' % (name, n))

L('')
L('  → C++ 는 `Montage_Play` 뒤에 `Montage_SetNextSection(이름, 같은 이름, M)` 을')
L('    부른다. 이름은 **박아 넣지 말고** `GetSectionName(0)` 으로 읽을 것.')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 2단계 통과 ===')
    L('⚠ 아직 **아무 동작도 안 바뀐다.** LocoSlot 에 몽타주를 거는 코드가 없어서')
    L('  슬롯은 상태 기계 포즈를 그대로 통과시킨다. 그게 3단계다.')
