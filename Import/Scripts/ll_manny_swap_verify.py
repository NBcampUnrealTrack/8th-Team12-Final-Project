"""마네퀸 교체 검산 + `Telling_A_Secret` 재삭제 — **새 프로세스**로 돌릴 것.

## ⚠⚠ `Telling_A_Secret` 이 되살아났다

§9-13 에서 지웠는데 다시 생겼다. **`git pull` 이 되살렸다.**

지운 상태가 `git status` 에서 ` D`(스테이지 안 된 워킹트리 삭제)였는데, 풀이
fast-forward 체크아웃을 돌면서 **워킹트리에 없는 파일을 복원**했다. 커밋에
그 파일 변경이 없어도 그렇게 된다.

⚠ 에셋을 지웠으면 **그 자리에서 커밋하거나**, 풀 뒤에 다시 확인할 것. 안 그러면
  조용히 돌아온다.

⚠ 이번에는 리타겟이 마네퀸판까지 만들어 놨으므로 **양쪽 다** 지운다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/AI/BP_Customer'
MANNY = '/Game/BurgerHouse/Anims/Manny'
fails = []

# ───────────────────────── Telling 재삭제
L('V === Telling_A_Secret 재삭제 ===')
DEAD = ('/Game/BurgerHouse/Anims/Montages/AM_Telling_A_Secret',
        '/Game/BurgerHouse/Anims/Mixamo/Anims/Telling_A_Secret',
        '%s/AM_Telling_A_Secret' % MANNY,
        '%s/Telling_A_Secret' % MANNY)
for p in DEAD:
    if not EAL.does_asset_exist(p):
        L('V   %-52s 이미 없다' % p.split('/')[-1])
        continue
    refs = EAL.find_package_referencers_for_asset(p, False) or []
    L('V   %-52s 참조 %s' % (p.split('/')[-1],
                            [str(r).split('/')[-1] for r in refs] or '없음'))
    L('V      delete -> %s' % EAL.delete_asset(p))

# ───────────────────────── BP_Customer
L('V')
L('V === BP_Customer ===')
cdo = unreal.get_default_object(EAL.load_asset(BP).generated_class())
mc = cdo.get_editor_property('mesh')
sm = mc.get_editor_property('skeletal_mesh')
ac = mc.get_editor_property('anim_class')


def check(label, got, want):
    ok = str(got) == str(want)
    L('V   %-22s %-56s %s' % (label, got, 'OK' if ok else '<-- 기대 %s' % want))
    if not ok:
        fails.append(label)


check('SkeletalMesh', sm.get_path_name() if sm else None,
      '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple')
check('Skeleton', sm.get_editor_property('skeleton').get_name() if sm else None, 'SK_Mannequin')
check('AnimClass 경로', ac.get_path_name() if ac else None,
      '/Game/BurgerHouse/Anims/Manny/ABP_Customer.ABP_Customer_C')
check('TraySocket', cdo.get_editor_property('tray_socket'), 'spine_05')

t = mc.get_relative_transform()
L('V   메시 상대 트랜스폼    loc %s · yaw %.0f' % (t.translation, t.rotation.rotator().yaw))

# ───────────────────────── Actions 가 전부 마네퀸판을 가리키나
L('V')
L('V === Actions — 전부 Manny 폴더를 가리키나 ===')
acts = cdo.get_editor_property('actions')
bad = 0
for k in sorted(acts.keys(), key=str):
    names = []
    for var in acts[k].get_editor_property('variants'):
        m = var.get_editor_property('montage')
        if not m:
            continue
        p = m.get_path_name()
        if not p.startswith(MANNY + '/'):
            E('V   %-18s %s 가 아직 옛 폴더다' % (k, p))
            fails.append('%s/%s' % (k, m.get_name()))
            bad += 1
        # 몽타주의 스켈레톤도 본다 — 폴더만 맞고 스켈레톤이 틀릴 수 있다
        sk = m.get_editor_property('skeleton')
        if sk and str(sk.get_name()) != 'SK_Mannequin':
            E('V   %-18s %s 스켈레톤이 %s' % (k, m.get_name(), sk.get_name()))
            fails.append('%s 스켈레톤' % m.get_name())
        names.append(m.get_name())
    L('V   %-18s %s' % (k, ' · '.join(names)))
L('V   옛 폴더 참조 %d개' % bad)

# ───────────────────────── 로코모션 이름이 Actions 키와 맞나
L('V')
L('V === 로코모션 이름 ===')
for p in ('loco_idle_action', 'loco_walk_action', 'loco_sit_action',
          'loco_carry_walk_action', 'loco_carry_idle_action'):
    v = str(cdo.get_editor_property(p))
    ok = v in [str(k) for k in acts.keys()]
    L('V   %-24s %-16s %s' % (p, v, 'OK' if ok else '<-- Actions 에 없다'))
    if not ok:
        fails.append(p)

# ───────────────────────── 옛 Mixamo 에셋을 아직 누가 참조하나
L('V')
L('V === 옛 Mixamo 애님을 아직 참조하는 곳 ===')
left = 0
for p in EAL.list_assets('/Game/BurgerHouse/Anims/Mixamo', recursive=True, include_folder=False):
    refs = EAL.find_package_referencers_for_asset(p, False) or []
    outside = [str(r) for r in refs if '/Anims/Mixamo' not in str(r)]
    if outside:
        L('V   %-56s <- %s' % (p.split('/')[-1], [x.split('/')[-1] for x in outside]))
        left += 1
L('V   %d개 (0 이면 Mixamo 폴더를 통째로 지워도 된다)' % left)

L('V')
if fails:
    E('V === 실패: %s ===' % fails)
else:
    L('V === 전부 통과 ===')
