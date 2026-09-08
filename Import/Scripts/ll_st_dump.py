"""ST_Customer 트리를 통째로 찍는다 — 상태·태스크·파라미터·전이·자식 순서.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" `
        -run=pythonscript -script=".../Import/Scripts/ll_st_dump.py" `
        -unattended -nopause -nosplash

    에디터가 켜져 있으면 Output Log 의 드롭다운을 `Python` 으로 바꾸고:
      exec(open(r'C:/Project/Unreal/Import/Scripts/ll_st_dump.py').read())

## 왜 이 스크립트가 있나

`.uasset` 에서 「이름이 있다」까지만 알 수 있다고 생각하기 쉬운데 **틀렸다.**
파라미터 값·`Tasks Completion`·전이·자식 순서가 전부 읽힌다. 「에디터에서 눈으로
확인」해야 했던 것들을 여기서 대신 센다.

⚠ **이식 직후 이걸로 리다이렉트가 먹었는지 확인한다** (`Import/BURGERHOUSE.md` §6).
   태스크 수가 39 보다 적으면 리다이렉트가 빠져 태스크가 사라진 것이다.

## ⚠ `EditorData` 는 프로퍼티가 아니다

    ST.get_editor_property('editor_data')            ← 실패한다
    unreal.load_object(ST, 'StateTreeEditorData_0')  ← 이렇게 잡는다

편집 가능(`EditAnywhere`) 표시가 없는 프로퍼티는 파이썬이 못 읽는다. 그래서
**서브오브젝트를 이름으로 직접 로드**한다. 블루프린트 그래프도 같은 방식이다
(`load_object(BP, 'AnimGraph')`).

## ⚠ 전이 트리거는 **비트플래그**라 숫자가 건너뛴다

    OnStateSucceeded 1 · OnStateFailed 2 · OnStateCompleted 3(=1|2)
    OnTick 4 · OnEvent 8 · OnDelegate 16
"""
import unreal

ASSET = '/Game/BurgerHouse/AI/ST_Customer'

TRIG = {0: 'None', 1: 'OnStateSucceeded', 2: 'OnStateFailed',
        3: 'OnStateCompleted', 4: 'OnTick', 8: 'OnEvent', 16: 'OnDelegate'}

L = unreal.log_warning


def gp(o, p, d='<X>'):
    try:
        return o.get_editor_property(p)
    except Exception:
        return d


def enum_int(v):
    """`unreal` 열거형 → int. ⚠ `int(v)` 는 안 된다 — 래퍼 객체다."""
    for attr in ('value',):
        if hasattr(v, attr):
            try:
                return int(getattr(v, attr))
            except Exception:
                pass
    try:
        return int(v)
    except Exception:
        return None


def enum_name(v):
    """`<StateTreeTaskCompletionType.ANY: 1>` → `ANY`"""
    s = str(v)
    if '.' in s and ':' in s:
        return s.split('.')[-1].split(':')[0]
    return s


def find_editor_data(st):
    """`EditorData` 서브오브젝트. 이름 뒤 숫자는 저장 시점마다 다를 수 있다."""
    for i in range(0, 8):
        o = None
        try:
            o = unreal.load_object(st, 'StateTreeEditorData_%d' % i)
        except Exception:
            pass
        if o is not None:
            return o
    return None


def desc(ed, node):
    """에디터에 보이는 태스크/조건 설명 ("Play Action Sit_Eat" 등)."""
    try:
        # <b> 같은 리치텍스트 태그가 섞여 나온다 — 벗긴다
        s = str(ed.get_node_description(node))
        for tag in ('<b>', '</>', '</b>'):
            s = s.replace(tag, '')
        return ' '.join(s.split())
    except Exception:
        return '?'


def walk(ed, s, depth, stats):
    pad = '  ' * depth
    name = str(gp(s, 'name'))
    stats['states'] += 1

    L('%s● %-12s  [completion=%s]' % (
        pad, name, enum_name(gp(s, 'tasks_completion'))))

    for c in (gp(s, 'enter_conditions', []) or []):
        L('%s    조건   %s' % (pad, desc(ed, c)))

    for t in (gp(s, 'tasks', []) or []):
        L('%s    태스크 %s' % (pad, desc(ed, t)))
        stats['tasks'] += 1
    single = gp(s, 'single_task', None)
    if single not in (None, '<X>'):
        d = desc(ed, single)
        if d and d != '?' and not d.startswith('None'):
            L('%s    태스크 %s   (single)' % (pad, d))
            stats['tasks'] += 1

    trs = gp(s, 'transitions', []) or []
    if not trs:
        L('%s    전이   (없음) ← 여기서 멈춘다' % pad)
    for tr in trs:
        stats['transitions'] += 1
        raw = gp(tr, 'trigger')
        n = enum_int(raw)
        trig = TRIG.get(n, enum_name(raw)) if n is not None else enum_name(raw)
        ev = ''
        try:
            ev = str(gp(gp(tr, 'required_event'), 'tag'))
            ev = ev[ev.find('=') + 1:].strip("')> \"") if '=' in ev else ''
        except Exception:
            pass
        delay = ''
        if gp(tr, 'b_delay_transition', False) is True:
            delay = '  [지연 %s]' % gp(tr, 'delay_duration')
        L('%s    전이   %-18s %-22s → %s%s' % (
            pad, trig, ev, gp(gp(tr, 'state'), 'name'), delay))

    for c in (gp(s, 'children', []) or []):
        walk(ed, c, depth + 1, stats)


def main():
    st = unreal.load_asset(ASSET)
    if not st:
        unreal.log_error('%s 를 못 찾았다' % ASSET)
        return

    ed = find_editor_data(st)
    if not ed:
        # ⚠ 여기서 멈춘다. 빈 트리를 「전이가 없다」로 착각하면 안 된다.
        unreal.log_error('=== 판정 불가 — EditorData 서브오브젝트를 못 찾았다 ===')
        unreal.log_error('    StateTreeEditorData_0..7 이 전부 없다. 에셋이 비었는지 볼 것')
        return

    stats = {'states': 0, 'tasks': 0, 'transitions': 0}
    L('===== %s =====' % ASSET)
    for s in gp(ed, 'sub_trees', []) or []:
        walk(ed, s, 0, stats)

    L('')
    L('--- 되읽기 ---')
    L('  상태 %d · 태스크 %d · 전이 %d' % (
        stats['states'], stats['tasks'], stats['transitions']))

    # ⚠ 이식 직후의 판정 기준. 리다이렉트가 빠지면 태스크가 **에러 없이** 준다.
    L('')
    if stats['tasks'] < 39 or stats['states'] < 32:
        unreal.log_error('  ⚠⚠ 이식 기준(상태 32 · 태스크 39 · 전이 29)보다 적다.')
        unreal.log_error('     Config/DefaultEngine.ini 의 CoreRedirects /')
        unreal.log_error('     GameplayTagRedirects 를 볼 것 — 빠진 리다이렉트가 있으면')
        unreal.log_error('     그 태스크는 이미 사라졌고, 저장하면 영영 없어진다.')
    else:
        L('  이식 기준(상태 32 · 태스크 39 · 전이 29)을 만족한다.')


main()
