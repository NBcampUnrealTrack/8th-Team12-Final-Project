"""FlowerHouse PIE 안정성 시험 — 맵을 열고 PIE 를 N초 돌린 뒤 스스로 닫는다

    UnrealEditor.exe Febricula.uproject -ExecCmds="py C:/Project/Unreal/Import/Scripts/fh_pie_test.py"

GPU 크래시(DXGI_ERROR_DEVICE_HUNG)는 PIE 시작 약 30초 뒤에 났다 (2026-09-30).
그보다 넉넉히 돌리고, 살아 있으면 10초마다 `[FHT] alive` 를 찍는다.
크래시가 나면 프로세스가 죽으므로 로그의 마지막 alive 와 DEVICE_HUNG 으로 판정한다.
"""
import unreal
import time
import os

LEVEL = '/Game/Levels/FlowerHouse/FlowerHouse'
RUN_S = float(os.environ.get('FH_PIE_SECONDS', '120'))
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(LEVEL)
st = {'t0': None, 'last': 0, 'phase': 'wait', 'ticks': 0}


def tick(dt):
    st['ticks'] += 1
    now = time.time()
    if st['phase'] == 'wait' and st['ticks'] > 120:
        les.editor_request_begin_play()
        st.update(phase='play', t0=now)
        unreal.log_warning('[FHT] PIE start')
    elif st['phase'] == 'play':
        el = now - st['t0']
        if el - st['last'] >= 10:
            st['last'] = el
            unreal.log_warning('[FHT] alive %.0fs' % el)
        if el >= RUN_S:
            les.editor_request_end_play()
            st.update(phase='end', t0=now)
            unreal.log_warning('[FHT] PIE end OK after %.0fs' % el)
    elif st['phase'] == 'end' and now - st['t0'] > 5:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.quit_editor()


h = unreal.register_slate_post_tick_callback(tick)
