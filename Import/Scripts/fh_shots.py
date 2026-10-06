"""FlowerHouse 확인용 스크린샷 — 에디터(GUI)를 띄워 찍고 스스로 닫는다

    UnrealEditor.exe Febricula.uproject -ExecCmds="py C:/Project/Unreal/Import/Scripts/fh_shots.py"

⚠ `-ExecutePythonScript` 는 스크립트가 끝나자마자 에디터를 닫는다 — 틱 콜백이
  한 번도 못 돈다. `-ExecCmds="py ..."` 로 띄워야 에디터가 살아 있다.
⚠ `AutomationLibrary.take_high_res_screenshot(카메라)` 는 요청만 찍히고 파일이
  안 남는 경우가 있었다. 레벨 뷰포트 카메라를 옮기고 `HighResShot` 콘솔
  명령으로 찍은 뒤, 새로 생긴 파일을 이름만 바꾼다.

레벨은 저장하지 않는다(뷰포트 카메라만 움직인다). 결과: Saved/Screenshots/FH_*.png
"""
import unreal
import os
import glob
import shutil

LEVEL = '/Game/Levels/FlowerHouse/FlowerHouse'
SHOT_DIR = r'C:/Project/Unreal/Saved/Screenshots'
M = 100.0
SHOTS = [
    ('door',  (0.0, 1.45, 1.55), (0.1, 7.2, 1.0)),
    ('wall',  (0.3, 5.9, 1.50), (2.95, 6.3, 1.35)),
    ('rack',  (0.05, 8.9, 1.25), (0.05, 10.3, 0.6)),
    ('street', (0.3, -7.5, 1.7), (0.0, 0.0, 1.7)),
    ('poster', (1.3, 8.6, 1.55), (2.95, 9.1, 1.55)),
    ('book', (1.80, 7.05, 1.30), (1.91, 7.2, 0.9)),
    ('corner', (-2.2, 9.9, 0.35), (-2.95, 10.59, 0.0)),
    ('clock', (-0.32, 9.7, 2.05), (-0.32, 10.55, 2.25)),
    ('card', (0.62, 2.84, 1.06), (0.975, 2.84, 0.935)),
    ('cases', (0.2, 3.2, 1.5), (-1.95, 6.0, 1.5)),
]
_only = [n for n in os.environ.get('FH_SHOTS', '').split(',') if n]
if _only:                                       # FH_SHOTS=poster,door
    SHOTS = [s for s in SHOTS if s[0] in _only]
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
les.load_level(LEVEL)


def ue(p):
    return unreal.Vector(p[0] * M, -p[1] * M, p[2] * M)


def pngs():
    return set(glob.glob(SHOT_DIR + '/**/*.png', recursive=True))


state = {'tick': 0, 'i': 0, 'phase': 'move', 'before': set(), 'at': 0}
SETTLE, WRITE = 240, 240


def tick(dt):
    state['tick'] += 1
    t = state['tick']
    i = state['i']
    if i >= len(SHOTS):
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.quit_editor()
        return
    name, loc, tgt = SHOTS[i]
    if state['phase'] == 'move':
        ues.set_level_viewport_camera_info(ue(loc), unreal.MathLibrary.make_rot_from_x(ue(tgt) - ue(loc)))
        state.update(phase='settle', at=t)
    elif state['phase'] == 'settle' and t - state['at'] >= (SETTLE * 3 if i == 0 else SETTLE):   # first shot: shaders still compiling
        state['before'] = __import__('time').time()
        unreal.SystemLibrary.execute_console_command(None, 'HighResShot 1600x900')
        state.update(phase='write', at=t)
    elif state['phase'] == 'write' and t - state['at'] >= WRITE:
        # HighResShot can reuse its file name - look at the write time
        new = sorted((p for p in pngs() if os.path.getmtime(p) >= state['before']
                      and not os.path.basename(p).startswith('FH_')), key=os.path.getmtime)
        if new:
            dst = os.path.join(SHOT_DIR, 'FH_%s.png' % name)
            shutil.copy2(new[-1], dst)
            unreal.log_warning('[FHS] %s -> %s' % (name, dst))
        else:
            unreal.log_warning('[FHS] %s: 파일이 안 생겼다' % name)
        state.update(i=i + 1, phase='move', at=t)


handle = unreal.register_slate_post_tick_callback(tick)
