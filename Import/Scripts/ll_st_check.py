"""`ST_Customer` · `BP_Customer` · `BP_CustomerAI` 배선 점검 (읽기 전용)

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_st_check.py" -AllowCommandletRendering

`Import/BURGERHOUSE.md` 가 말하는 배선을 실제 에셋에서 되읽는다.
**아무것도 고치지 않는다** — 저장도 안 한다.

⚠ §6.0 ②-c — 「썼다」가 아니라 「읽으니 그 값이더라」. 특히 스키마의
  `Context Actor Class` 는 틀리면 **모든 태스크가 조용히 실패**하는 값이라
  눈으로 확인하는 것보다 되읽는 편이 확실하다.
"""
import unreal

ST_PATH = '/Game/BurgerHouse/BP/ST_Customer'
BP_PAWN = '/Game/BurgerHouse/BP/BP_Customer'
BP_CTRL = '/Game/BurgerHouse/BP/BP_CustomerAI'

W = unreal.log_warning
E = unreal.log_error


def safe(fn, default='(읽기 실패)'):
    """⚠ 삼키지 않는다 — 실패는 값으로 남겨 화면에 드러낸다."""
    try:
        return fn()
    except Exception as ex:
        return '%s: %s' % (default, ex)


def node_name(n):
    """`FStateTreeEditorNode` 에서 노드 구조체 이름을 뽑는다.

    ⚠ `Node` 는 `FInstancedStruct` 라 파이썬 노출이 판본마다 다르다.
      여러 경로를 시도하고, 전부 실패하면 **실패했다고 적는다.**
    """
    for get in (
        lambda: n.get_editor_property('node').get_script_struct().get_name(),
        lambda: n.get_editor_property('node').get_script_struct().get_fname(),
        lambda: str(n.get_editor_property('node')),
    ):
        try:
            v = get()
            if v:
                return str(v)
        except Exception:
            continue
    return '(노드 이름 못 읽음)'


def dump_state(s, depth=0):
    pad = '   ' * depth
    name = safe(lambda: str(s.get_editor_property('name')))
    stype = safe(lambda: str(s.get_editor_property('type')))
    sel = safe(lambda: str(s.get_editor_property('selection_behavior')))

    tasks = safe(lambda: list(s.get_editor_property('tasks')), None) or []
    trans = safe(lambda: list(s.get_editor_property('transitions')), None) or []
    kids = safe(lambda: list(s.get_editor_property('children')), None) or []

    W('%s▸ %s   [%s / %s]' % (pad, name, stype.split('.')[-1], sel.split('.')[-1]))

    if isinstance(tasks, list) and tasks:
        for i, t in enumerate(tasks):
            W('%s    Task %d: %s' % (pad, i + 1, node_name(t)))
    else:
        W('%s    Task 없음' % pad)

    if isinstance(trans, list):
        for tr in trans:
            trig = safe(lambda: str(tr.get_editor_property('trigger')))
            tgt = safe(lambda: str(tr.get_editor_property('state')))
            tag = ''
            try:
                ev = tr.get_editor_property('required_event')
                t = ev.get_editor_property('tag')
                if t and str(t) not in ('', 'None'):
                    tag = '  [%s]' % t
            except Exception:
                pass
            W('%s    → %s%s  ⇒ %s'
              % (pad, trig.split('.')[-1], tag, str(tgt).split('.')[-1]))

    for k in kids:
        dump_state(k, depth + 1)


def find_editor_data(st):
    """`UStateTree::EditorData` 에 닿는다.

    ⚠ **`get_editor_property('editor_data')` 는 안 된다.** 그 UPROPERTY 에는
      지정자가 하나도 없어서(`UPROPERTY()`) 파이썬에 노출되지 않는다.
      그래서 서브오브젝트를 직접 뒤진다.
    """
    # ① 이름으로 직접
    #
    # ⚠ 실제 이름은 `StateTreeEditorData_0` 처럼 **뒤에 숫자가 붙는다.**
    #   숫자 없는 이름만 찾으면 늘 실패하고 「EditorData 에 못 닿았다」로
    #   오진한다 — 실제로는 멀쩡한데.
    for nm in ['EditorData', 'StateTreeEditorData'] + \
              ['StateTreeEditorData_%d' % i for i in range(0, 8)]:
        try:
            o = unreal.load_object(st, nm)
            if o:
                return o
        except Exception:
            pass

    # ② 이 에셋 패키지 안에서 클래스로 찾는다
    try:
        pkg = st.get_outer()
        for o in unreal.EditorAssetLibrary.load_asset(ST_PATH).get_outer().get_all_objects() \
                if hasattr(pkg, 'get_all_objects') else []:
            if 'EditorData' in o.get_class().get_name():
                return o
    except Exception:
        pass

    # ③ 전역에서 이 에셋에 딸린 EditorData 를 찾는다
    try:
        for o in unreal.find_objects_of_class(unreal.load_class(None,
                 '/Script/StateTreeEditorModule.StateTreeEditorData')):
            try:
                if o.get_outer() == st:
                    return o
            except Exception:
                continue
    except Exception:
        pass

    return None


def diagnose(st):
    """무엇이 노출돼 있는지 찍는다 — 추측을 반복하지 않으려는 것이다."""
    W('')
    W('[진단] StateTree 에셋에서 파이썬으로 읽을 수 있는 것')
    try:
        names = sorted(n for n in dir(st) if not n.startswith('_'))
        W('  속성/메서드 %d개' % len(names))
        W('  %s' % ', '.join(names[:60]))
    except Exception as ex:
        E('  dir() 실패: %s' % ex)


def dump_blueprints():
    W('')
    W('[BP_Customer]')
    bp = unreal.EditorAssetLibrary.load_asset(BP_PAWN)
    if not bp:
        E('  ❌ 없음')
    else:
        cdo = safe(lambda: unreal.get_default_object(bp.generated_class()), None)
        if cdo and not isinstance(cdo, str):
            ap = safe(lambda: str(cdo.get_editor_property('auto_possess_ai')))
            W('  Auto Possess AI  : %s' % ap)
            if 'PLACED_IN_WORLD_OR_SPAWNED' in ap.upper() or 'SPAWNED' in ap.upper():
                W('    ✅')
            else:
                E('    ❌❌ StateTree 가 시작조차 안 한다 (가이드 §0-2)')
            W('  AI Controller    : %s'
              % safe(lambda: str(cdo.get_editor_property('ai_controller_class'))))
            W('  Actions(애님) 수 : %s'
              % safe(lambda: len(cdo.get_editor_property('actions'))))
            W('  Tray Socket      : %s'
              % safe(lambda: str(cdo.get_editor_property('tray_socket'))))
            W('  Order Number     : %s'
              % safe(lambda: cdo.get_editor_property('order_number')))
        else:
            E('  ❌ CDO 를 못 읽었다: %s' % cdo)

    W('')
    W('[BP_CustomerAI]')
    bpc = unreal.EditorAssetLibrary.load_asset(BP_CTRL)
    if not bpc:
        E('  ❌ 없음')
    else:
        cdo = safe(lambda: unreal.get_default_object(bpc.generated_class()), None)
        if cdo and not isinstance(cdo, str):
            comp = safe(lambda: cdo.get_editor_property('state_tree_ai'), None)
            if comp and not isinstance(comp, str):
                # ⚠ `state_tree_ref` 를 그대로 찍으면 `<Struct 'StateTreeReference'
                #   ... {}>` 만 나온다 — **구조체 안쪽까지 들어가야** 에셋이 보인다.
                #   겉만 보고 판정하면 꽂혀 있는데도 「안 꽂혔다」가 된다.
                refstruct = safe(lambda: comp.get_editor_property('state_tree_ref'), None)
                ref = safe(lambda: refstruct.get_editor_property('state_tree').get_path_name())
                W('  StateTree Ref    : %s' % ref)
                if '읽기 실패' in ref or ref == 'None':
                    W('    ⚠ 판정 불가 — 프로퍼티를 못 읽었다.')
                    W('      「못 읽음」은 「안 꽂힘」이 아니다 (§6.0 ②-c).')
                    W('      .uasset 을 직접 훑거나 에디터에서 눈으로 볼 것')
                elif 'ST_Customer' in ref:
                    W('    ✅')
                else:
                    E('    ❌❌ ST_Customer 가 안 꽂혔다 — 트리가 안 돈다 (가이드 §4-2)')
            else:
                W('  ⚠ StateTreeAI 컴포넌트를 CDO 로 못 읽었다 (%s)' % comp)
                W('    컴포넌트 템플릿은 파이썬 노출이 제한적이다 — 에디터에서 눈으로 볼 것')
        else:
            E('  ❌ CDO 를 못 읽었다: %s' % cdo)


def main():
    W('=' * 60)
    W('ST_Customer 배선 점검')
    W('=' * 60)

    st = unreal.EditorAssetLibrary.load_asset(ST_PATH)
    if not st:
        E('  ❌ %s 를 못 찾았다' % ST_PATH)
        return

    ed = find_editor_data(st)
    if ed is None:
        E('  ❌ EditorData 에 못 닿았다 — 아래 [진단] 참고')
        diagnose(st)
        dump_blueprints()          # ⚠ BP 점검은 독립이다. 여기서 멈추지 않는다
        return

    # ---- 스키마 — 가이드 §1-2 · §1-3 ----
    W('')
    W('[스키마]')
    schema = safe(lambda: ed.get_editor_property('schema'), None)
    if not schema or isinstance(schema, str):
        E('  ❌ 스키마 없음: %s' % schema)
    else:
        cls = safe(lambda: schema.get_class().get_name())
        W('  클래스           : %s' % cls)
        if 'AIComponent' in str(cls):
            W('    ✅ StateTree AI Component — 맞다')
        else:
            E('    ❌ StateTree AI Component 가 아니다 → Customer 바인딩이 통째로 빈다')

        ctx = safe(lambda: schema.get_editor_property('context_actor_class'))
        W('  Context Actor    : %s' % ctx)
        if 'Customer' in str(ctx):
            W('    ✅ BP_Customer 계열 — 자동 바인딩 후보가 하나로 좁혀진다')
        else:
            E('    ❌❌ BP_Customer 가 아니다 — 가이드 §1-3.')
            E('        Actor 로 두면 후보가 0이라 컴파일 에러,')
            E('        AActor 계열이면 AIController 가 대신 꽂힌다')

        aic = safe(lambda: schema.get_editor_property('ai_controller_class'))
        W('  AIController     : %s' % aic)

    # ---- 상태 구조 ----
    W('')
    W('[상태 구조]')
    subtrees = safe(lambda: list(ed.get_editor_property('sub_trees')), None) or []
    if not isinstance(subtrees, list) or not subtrees:
        E('  ❌ SubTrees 가 비었다 — 상태가 하나도 없다')
    else:
        for root in subtrees:
            dump_state(root, 0)

    dump_blueprints()

    W('')
    W('=' * 60)


main()
