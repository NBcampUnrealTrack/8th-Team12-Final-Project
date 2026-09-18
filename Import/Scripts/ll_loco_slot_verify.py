"""1단계 검산 — 로코모션 슬롯 배선이 **디스크에 남았는지** 새 프로세스에서 본다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).

기대 체인 —

    [Locomotion SM] → Slot 'LocoSlot' → Save cached 'LocoCache'
                                          ├→ Layered blend .BasePose
                                          └→ Slot 'UpperBody' → .BlendPoses_0
                                       Layered blend → Slot 'DefaultSlot' → Output
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
GRAPH = 'AnimGraph'
BPS = unreal.BlueprintService

EXPECT = [
    ('Locomotion State Machine', 'Pose', "Slot 'LocoSlot'", 'Source'),
    ("Slot 'LocoSlot'", 'Pose', "Save cached pose 'LocoCache'", 'Pose'),
    ("Use cached pose 'LocoCache'", 'Pose', 'Layered blend per bone', 'BasePose'),
    ("Use cached pose 'LocoCache'", 'Pose', "Slot 'UpperBody'", 'Source'),
    ("Slot 'UpperBody'", 'Pose', 'Layered blend per bone', 'BlendPoses_0'),
    ('Layered blend per bone', 'Pose', "Slot 'DefaultSlot'", 'Source'),
    ("Slot 'DefaultSlot'", 'Pose', 'Output Pose', 'Result'),
]

fails = []
got = []
for c in BPS.get_connections(ABP, GRAPH) or []:
    got.append((' '.join(str(c.source_node_title).split()), str(c.source_pin_name),
                ' '.join(str(c.target_node_title).split()), str(c.target_pin_name)))

L('=== 실제 배선 %d개 ===' % len(got))
for s, sp, t, tp in got:
    L('  %-42s .%-13s -> %-42s .%s' % (s, sp, t, tp))

L('')
L('=== 기대 체인 대조 ===')
for es, esp, et, etp in EXPECT:
    hit = any(es in s and esp == sp and et in t and etp == tp for s, sp, t, tp in got)
    L('  %-30s .%-12s -> %-30s .%-12s  %s'
      % (es, esp, et, etp, 'OK' if hit else '<-- ⚠ 없다'))
    if not hit:
        fails.append('%s.%s -> %s.%s 가 없다' % (es, esp, et, etp))

if len(got) != len(EXPECT):
    fails.append('연결이 %d개다 (기대 %d개) — 남거나 빠진 선이 있다' % (len(got), len(EXPECT)))

# ---- 컴파일 상태 ----
L('')
L('=== 컴파일 상태 ===')
# ⚠ `status` 는 구조체가 아니라 **튜플**로 돌아온다 (판본 차이). `get_editor_property`
#   를 부르면 AttributeError 라, 문자열로 찍고 키워드로 판정한다.
d = BPS.describe_graph(ABP, GRAPH)
try:
    # ⚠ `describe_graph` 는 **튜플**을 돌려준다 (구조체가 아니다). 통째로 문자열로
    #   만들어 `status: {...}` 부분만 잘라 쓴다.
    raw = str(d)
    st = raw[raw.rfind('status:'):] if 'status:' in raw else raw[-300:]
    L('  %s' % st)
    if 'has_compile_errors: True' in st:
        fails.append('컴파일 에러가 있다')
    if 'BS_Error' in st:
        fails.append('컴파일 상태가 BS_Error 다')
    if 'has_compile_warnings: True' in st:
        L('  ⚠ 컴파일 경고가 있다 — 에디터에서 내용을 볼 것')
except Exception as ex:
    E('  status 를 못 읽음: %s' % str(ex)[:90])

# ---- 슬롯이 아직 아무것도 안 덮는가 (1단계에서는 몽타주가 없어야 정상) ----
L('')
L('=== 1단계에서는 동작이 변하지 않아야 한다 ===')
L('  `LocoSlot` 에 재생되는 몽타주가 아직 없으므로 슬롯은 Source 를 그대로')
L('  통과시킨다 — 즉 상태 기계 포즈가 그대로 나온다. PIE 에서 **지금과 똑같아야**')
L('  정상이다. 달라 보이면 배선이 틀린 것이다.')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 — 슬롯이 디스크에 남았고 체인이 맞다 ===')
