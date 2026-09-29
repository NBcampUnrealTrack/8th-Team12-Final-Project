"""이 프로젝트가 **실제로 쓰고 있는** idle 모션을 훑는다. 아무것도 안 고친다.

파일 이름에 `Idle` 이 들어간 에셋은 70개가 넘지만 대부분 마네퀸·MetaHuman 템플릿이
딸려온 것이다. 「쓰고 있다」는 **누가 참조하느냐**로만 알 수 있다.

보는 것 —
  ① idle 에셋마다 참조하는 패키지 (= 쓰는 쪽)
  ② 아무도 참조 안 하는 것 (= 에셋만 있고 안 쓰임)
  ③ ABP 들의 상태 기계에 있는 Idle 상태
  ④ 캐릭터 BP 가 어느 ABP 를 물고 있나
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

ANIM_CLASSES = ('AnimSequence', 'BlendSpace', 'BlendSpace1D', 'AnimMontage',
                'AimOffsetBlendSpace', 'AimOffsetBlendSpace1D')

SKIP_DIRS = ('/Game/Plugins',)


def short(p):
    return str(p).replace('/Game/', '')


# ---- idle 에셋 모으기 ----
assets = []
for d in REG.get_assets_by_path('/Game', recursive=True):
    cls = str(d.asset_class_path.asset_name)
    if cls not in ANIM_CLASSES:
        continue
    name = str(d.asset_name)
    if 'idle' not in name.lower():
        continue
    pkg = str(d.package_name)
    if any(pkg.startswith(s) for s in SKIP_DIRS):
        continue
    assets.append((pkg, name, cls))
assets.sort()

L('=== idle 에셋 %d개 · 참조하는 쪽 ===' % len(assets))
L('')
used, unused = [], []
for pkg, name, cls in assets:
    refs = [short(r) for r in (EAL.find_package_referencers_for_asset(pkg, False) or [])]
    # 자기 폴더 안에서만 참조되는 것도 있다 — 전부 보여주고 판단은 사람이 한다
    if refs:
        used.append((pkg, name, cls, refs))
    else:
        unused.append((pkg, name, cls))

L('--- 쓰이는 것 %d개 ---' % len(used))
for pkg, name, cls, refs in used:
    L('  %-42s %-12s <- %s' % (short(pkg), cls, ', '.join(refs)))

L('')
L('--- 아무도 참조 안 하는 것 %d개 ---' % len(unused))
for pkg, name, cls in unused:
    L('  %-42s %s' % (short(pkg), cls))

# ---- ABP 들의 Idle 상태 ----
L('')
L('=== ABP 의 상태 기계 · Idle 관련 상태 ===')
abps = []
for d in REG.get_assets_by_path('/Game', recursive=True):
    if str(d.asset_class_path.asset_name) != 'AnimBlueprint':
        continue
    pkg = str(d.package_name)
    if any(pkg.startswith(s) for s in SKIP_DIRS):
        continue
    abps.append(pkg)
abps.sort()

for pkg in abps:
    try:
        machines = unreal.AnimGraphService.list_state_machines(pkg) or []
    except Exception as ex:
        E('  %-46s 조회 실패: %s' % (short(pkg), str(ex)[:60]))
        continue
    if not machines:
        continue
    # ⚠ 상태 기계 이름(노드 제목)에 줄바꿈이 들어 있는 경우가 있다 — 로그가 잘린다
    lines = []
    for m in machines:
        mn = ' '.join(str(m.machine_name).split())
        states = [' '.join(str(s.state_name).split())
                  for s in (unreal.AnimGraphService.list_states_in_machine(
                      pkg, m.machine_name) or [])
                  if str(s.state_type) == 'State']
        lines.append('[%s] %s' % (mn, ' · '.join(states)))
    L('  %-46s %s' % (short(pkg), ' | '.join(lines)))

    # 이 ABP 가 참조하는 idle 에셋
    deps = [short(x) for x in (EAL.find_package_referencers_for_asset(pkg, False) or [])]
    mine = [short(p) for p, n, c, r in used if short(pkg) in r]
    if mine:
        L('      -> idle 에셋: %s' % ', '.join(mine))
    if deps:
        L('      <- 이 ABP 를 쓰는 쪽: %s' % ', '.join(deps))

L('')
L('=== 끝 — 아무것도 안 고쳤다 ===')
