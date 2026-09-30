"""`RTG_MannyWalk` 의 op 들이 실제로 무슨 설정을 노출하는지 **덤프**한다.

⚠ 골반 전진을 어디서 끄는지 문서로는 안 나온다. 추측하지 말고 본다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

rtg = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Retarget/RTG_MannyWalk')
rc = unreal.IKRetargeterController.get_controller(rtg)
n = rc.get_num_retarget_ops()
L('op %d개' % n)
for i in range(n):
    name = str(rc.get_op_name(i))
    L('')
    L('=== [%d] %s  (enabled=%s) ===' % (i, name, rc.get_retarget_op_enabled(i)))
    oc = rc.get_op_controller(i)
    if not oc:
        E('  컨트롤러 없음')
        continue
    L('  controller: %s' % oc.get_class().get_name())
    L('  controller 메서드: %s'
      % [m for m in dir(oc) if not m.startswith('_') and 'setting' in m.lower()])
    if not hasattr(oc, 'get_settings'):
        continue
    s = oc.get_settings()
    L('  settings: %s' % s.get_class().get_name() if hasattr(s, 'get_class') else type(s))
    for p in sorted(dir(s)):
        if p.startswith('_'):
            continue
        try:
            v = s.get_editor_property(p)
        except Exception:
            continue
        L('      %-40s %s' % (p, v))

L('')
L('=== 소스/타깃 리그의 retarget root ===')
for who, lbl in ((unreal.RetargetSourceOrTarget.SOURCE, '소스'),
                 (unreal.RetargetSourceOrTarget.TARGET, '타깃')):
    rig = rc.get_ik_rig(who)
    if rig:
        c = unreal.IKRigController.get_controller(rig)
        L('  %s %-20s root=%s' % (lbl, rig.get_name(), c.get_retarget_root()))

L('')
L('=== AnimSequence 트랙을 직접 고칠 수 있나 ===')
for m in sorted(dir(unreal.AnimationLibrary)):
    if any(k in m for k in ('track', 'key', 'bone_')) and not m.startswith('_'):
        L('  AnimationLibrary.%s' % m)
