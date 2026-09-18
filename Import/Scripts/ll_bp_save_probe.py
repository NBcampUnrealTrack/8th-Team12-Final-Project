"""`BP_Customer` 가 왜 안 써지는지 저장 경로를 하나씩 시험한다.

⚠ 되읽기는 **메모리**를 본다. 여기서는 그것 말고 `save_*` 의 **반환값**과
  패키지 상태만 본다. 실제 기록 여부는 바깥에서 파일 수정 시각으로 잰다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
BP_PATH = '/Game/BurgerHouse/AI/BP_Customer'

bp = EAL.load_asset(BP_PATH)
pkg = bp.get_outermost()
L('패키지: %s' % pkg.get_name())
for m in ('is_dirty', 'mark_package_dirty', 'get_path_name'):
    L('  Package.%-22s %s' % (m, hasattr(pkg, m)))

L('')
L('=== ① 컴파일 없이, 값 변경 없이 그냥 저장 ===')
L('  save_asset(only_if_is_dirty=False)  -> %s'
  % EAL.save_asset(BP_PATH, only_if_is_dirty=False))
L('  save_packages(only_dirty=False)     -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

L('')
L('=== ② CDO 를 실제로 바꾼 뒤 (컴파일 없이) ===')
cdo = unreal.get_default_object(bp.generated_class())
before = cdo.get_editor_property('decoration_seed')
cdo.set_editor_property('decoration_seed', before + 1)
L('  decoration_seed %s -> %s' % (before, cdo.get_editor_property('decoration_seed')))
if hasattr(pkg, 'is_dirty'):
    L('  패키지 dirty? %s' % pkg.is_dirty())
if hasattr(pkg, 'mark_package_dirty'):
    L('  mark_package_dirty() -> %s' % pkg.mark_package_dirty())
L('  save_asset      -> %s' % EAL.save_asset(BP_PATH, only_if_is_dirty=False))
L('  save_packages   -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

L('')
L('=== ③ 다른 경로들 ===')
for name, fn in (
    ('save_loaded_asset',
     lambda: EAL.save_loaded_asset(bp, only_if_is_dirty=False)),
    # ⚠ `save_directory` 는 **쓰지 말 것.** 한 번 넣었다가 같은 폴더의
    #   `BP_CustomerAI` · `ST_Customer` 까지 재저장됐다. ST_Customer 는
    #   12만 바이트가 바뀌는 재직렬화라 디프가 통째로 더러워진다.
):
    try:
        L('  %-24s -> %s' % (name, fn()))
    except Exception as ex:
        E('  %-24s 예외: %s' % (name, str(ex)[:90]))

L('')
L('=== ④ 되돌리기 (decoration_seed 원복) ===')
cdo.set_editor_property('decoration_seed', before)
L('  decoration_seed -> %s' % cdo.get_editor_property('decoration_seed'))
L('  save_packages   -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
