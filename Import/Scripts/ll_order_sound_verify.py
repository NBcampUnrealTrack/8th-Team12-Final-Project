"""주문 알림음 배선을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6). 레벨 액터는 특히 그렇다.

체인 —

    K_Act (Play Action) 완료 → K_Order 진입
                                 ├ Place Order
                                 └ Play Sound At Tagged Actor  ← 태그로 액터를 찾아 울린다
                                        ↓
                             레벨의 AmbientSound (Tags: OrderChime)
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

ST = '/Game/BurgerHouse/AI/ST_Customer'
STATE = 'Root/Kiosk/K_Order'
TASK = 'BurgerHousePlaySoundTask'
TAG = 'OrderChime'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'

S = unreal.StateTreeService
fails = []

# ---- ① 트리 ----
L('=== ① ST_Customer ===')
info = S.get_state_tree_info(ST)
states = {str(s.get_editor_property('path')): s
          for s in (info.get_editor_property('all_states') if info else [])}

for path in ('Root/Kiosk/K_Act', STATE):
    st = states.get(path)
    if not st:
        fails.append('%s 상태가 없다' % path)
        continue
    names = ['%s (%s)' % (t.get_editor_property('name'), t.get_editor_property('struct_type'))
             for t in st.get_editor_property('tasks')]
    trs = ['%s -> %s' % (t.get_editor_property('trigger'),
                         t.get_editor_property('target_state_name'))
           for t in st.get_editor_property('transitions')]
    L('  %-22s 태스크 %s' % (path.split('/')[-1], names))
    L('  %-22s 전이   %s' % ('', trs))

# K_Act 가 완료되면 K_Order 로 가는가 (= 「Play Action 완료 다음」의 근거)
kact = states.get('Root/Kiosk/K_Act')
if kact:
    hit = any(str(t.get_editor_property('trigger')) == 'OnStateCompleted'
              and str(t.get_editor_property('target_state_name')) == 'K_Order'
              for t in kact.get_editor_property('transitions'))
    L('')
    L('  K_Act 의 Play Action 완료 -> K_Order  %s' % ('OK' if hit else '<-- ⚠ 없다'))
    if not hit:
        fails.append('K_Act 가 완료 시 K_Order 로 안 간다 — 타이밍 전제가 깨진다')

korder = states.get(STATE)
if korder:
    has = any(str(t.get_editor_property('struct_type')) == TASK
              for t in korder.get_editor_property('tasks'))
    if not has:
        fails.append('K_Order 에 %s 가 없다' % TASK)

L('')
L('  컴파일 상태: is_compiled=%s · %s'
  % (info.get_editor_property('is_compiled'),
     info.get_editor_property('last_compile_status')))

# ---- ② 태스크 파라미터 ----
L('')
L('=== ② 태스크 파라미터 ===')
props = {}
for p in S.get_task_property_names(ST, STATE, TASK) or []:
    props[str(p.get_editor_property('name'))] = str(p.get_editor_property('current_value'))
    L('  %-18s %s' % (p.get_editor_property('name'), p.get_editor_property('current_value')))
if props.get('ActorTag') != TAG:
    fails.append('ActorTag 가 %s 다 (기대 %s)' % (props.get('ActorTag'), TAG))

# ---- ③ 레벨 액터 ----
L('')
L('=== ③ 레벨의 소리 액터 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
tagged = unreal.GameplayStatics.get_all_actors_with_tag(world, unreal.Name(TAG))
L('  태그 «%s» 액터 %d개' % (TAG, len(tagged)))
if not tagged:
    fails.append('태그 %s 를 가진 액터가 레벨에 없다 — 태스크가 못 찾는다' % TAG)

sound_missing = []
for a in tagged:
    ac = a.get_editor_property('audio_component') if hasattr(a, 'get_editor_property') else None
    if ac is None:
        ac = a.get_component_by_class(unreal.AudioComponent)
    snd = ac.get_editor_property('sound') if ac else None
    auto = ac.get_editor_property('auto_activate') if ac else None
    L('  %-18s Sound=%s · AutoActivate=%s' % (a.get_actor_label(), snd, auto))
    if auto:
        fails.append('%s 의 AutoActivate 가 켜져 있다 — 레벨이 열리자마자 계속 운다'
                     % a.get_actor_label())
    if not snd:
        sound_missing.append(a.get_actor_label())

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 배선 통과 ===')

if sound_missing:
    L('')
    L('⚠⚠ **아직 소리는 안 난다** — %s 에 Sound 에셋이 비어 있다.' % ', '.join(sound_missing))
    L('   무엇을 울릴지는 취향이라 고르지 않았다. 디테일 패널에서 붙이면 끝이다.')
    L('   그 전까지는 PIE 로그에 이렇게 찍힌다:')
    L('     LogBurgerHouseST: Warning: Play Sound — ... AudioComponent 에 Sound 가 비어 있다')
