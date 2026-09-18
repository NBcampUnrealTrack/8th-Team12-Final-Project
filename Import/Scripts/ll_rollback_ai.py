# -*- coding: utf-8 -*-
"""BP 폴더로 잘못 옮긴 에셋을 AI 폴더로 **되돌린다**.

    에디터를 끄고 돌린다.

## ⚠⚠ 파일만 옮기면 안 된다

옮길 때 레벨(`L_BurgerHouse.umap`)이 **`/Game/BurgerHouse/BP/...` 로 다시
저장됐다.** 디스크에서 파일만 되돌리면 레벨이 없는 에셋을 물어 손님이 통째로
사라진다.

## ⚠ git 으로도 못 되돌린다

이번 세션 작업이 전부 미커밋이라 `git checkout` 은 **몇 주 전 버전**으로
돌아간다. `BP_CounterTray`·`BP_TableTray` 는 아예 추적되지도 않는다.

## 순서

```
1. AI 에 남은 리다이렉터 3개를 fixup      -> 참조가 BP 를 직접 가리키고 리다이렉터가 사라진다
2. BP/X -> AI/X 이름변경 5개              -> 참조가 AI 로 오고 BP 에 새 리다이렉터가 생긴다
3. BP 의 새 리다이렉터를 fixup            -> 참조가 AI 로 확정되고 리다이렉터가 사라진다
4. 전부 저장 -> BP 폴더 삭제
```

⚠ 1번을 건너뛰면 2번에서 **대상 경로에 리다이렉터가 이미 있어** 이름변경이
  조용히 실패한다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

AI = '/Game/BurgerHouse/BP'
BP = '/Game/BurgerHouse/BP'
NAMES = ['BP_Customer', 'BP_CustomerAI', 'BP_CounterTray', 'BP_TableTray', 'ST_Customer']

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(['/Game/BurgerHouse'], True)
fails = []


def redirectors(path):
    out = []
    for a in ar.get_assets_by_path(path, recursive=True, include_only_on_disk_assets=False):
        if str(a.asset_class_path.asset_name) != 'ObjectRedirector':
            continue
        o = unreal.load_asset('%s.%s' % (a.package_name, a.asset_name))
        if o:
            out.append(o)
    return out


def listing(path, tag):
    rows = []
    for a in ar.get_assets_by_path(path, recursive=True, include_only_on_disk_assets=False):
        rows.append('%-16s %s' % (a.asset_name, a.asset_class_path.asset_name))
    L('B   %-4s %d개' % (tag, len(rows)))
    for r in sorted(rows):
        L('B       %s' % r)


L('B === 시작 상태 ===')
listing(AI, 'AI')
listing(BP, 'BP')

# ───────────────────────── 1) AI 의 옛 리다이렉터를 정리한다
L('B')
L('B === 1) AI 리다이렉터 정리 ===')
rs = redirectors(AI)
L('B   찾은 리다이렉터 %d개: %s' % (len(rs), [r.get_name() for r in rs]))
if rs:
    AT.fixup_referencers(rs)
after = redirectors(AI)
L('B   정리 후 남은 리다이렉터 %d개' % len(after))
if after:
    E('B   ⚠ 안 지워졌다 — 손으로 지운다')
    for r in after:
        p = r.get_path_name().split('.')[0]
        L('B     delete %s -> %s' % (p, EAL.delete_asset(p)))

# ───────────────────────── 2) 되돌린다
L('B')
L('B === 2) BP -> AI 이름변경 ===')
for n in NAMES:
    src, dst = '%s/%s' % (BP, n), '%s/%s' % (AI, n)
    if not EAL.does_asset_exist(src):
        L('B   %-16s BP 에 없다 — 건너뜀' % n)
        continue
    if EAL.does_asset_exist(dst):
        E('B   %-16s 대상이 아직 있다 (%s) — 이름변경 못 한다' % (n, dst))
        fails.append(n)
        continue
    ok = EAL.rename_asset(src, dst)
    L('B   %-16s %s -> %s  %s' % (n, src, dst, 'OK' if ok else '<-- 실패'))
    if not ok:
        fails.append(n)

# ───────────────────────── 3) BP 에 생긴 새 리다이렉터를 정리한다
L('B')
L('B === 3) BP 리다이렉터 정리 ===')
rs = redirectors(BP)
L('B   찾은 리다이렉터 %d개: %s' % (len(rs), [r.get_name() for r in rs]))
if rs:
    AT.fixup_referencers(rs)
after = redirectors(BP)
L('B   정리 후 남은 리다이렉터 %d개' % len(after))
for r in after:
    p = r.get_path_name().split('.')[0]
    L('B     delete %s -> %s' % (p, EAL.delete_asset(p)))

# ───────────────────────── 4) 저장하고 BP 폴더를 지운다
L('B')
L('B === 4) 저장 / BP 폴더 삭제 ===')
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
L('B   save_dirty_packages 완료')

left = [str(a.asset_name) for a in ar.get_assets_by_path(BP, recursive=True,
                                                         include_only_on_disk_assets=False)]
if left:
    E('B   ⚠ BP 에 아직 %d개 남아 있다: %s — 폴더를 안 지운다' % (len(left), left))
    fails.append('BP 잔여')
else:
    L('B   delete_directory %s -> %s' % (BP, EAL.delete_directory(BP)))

L('B')
L('B === 끝 상태 ===')
listing(AI, 'AI')
listing(BP, 'BP')

L('B')
if fails:
    E('B === 실패: %s ===' % fails)
else:
    L('B === 완료 ===')
