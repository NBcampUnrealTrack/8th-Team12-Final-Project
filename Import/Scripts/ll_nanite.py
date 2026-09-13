"""VSM 경고 대응 — Nanite 활성화 + 불필요한 그림자 끄기

⚠ **에디터가 열려 있으면 에디터 안에서 실행할 것** (헤드리스 커맨드릿과
  동시에 열어 두면 저장이 충돌한다):

    Window > Output Log > 콘솔을 `Python` 으로 바꾸고
    exec(open(r'C:/Project/Unreal/Import/Scripts/ll_nanite.py').read())

## 무슨 경고인가

    LogRenderer: Warning: [VSM] Non-Nanite Marking Job Queue overflow.

VSM(Virtual Shadow Map)은 Nanite 메쉬는 효율적으로 처리하지만 **non-Nanite
메쉬는 그림자맵 페이지에 일일이 마킹**해야 한다. 그 큐가 넘쳤다는 뜻이다.
우리 씬이 정확히 그 조건 — **non-Nanite 인스턴스 334개가 넓은 면적을 덮는다.**

⚠ 머티리얼과는 **무관**하다. 순수하게 그림자·성능 쪽 경고다.

## 두 가지를 한다

**① Nanite 활성화** — 지오메트리가 있는 것에만. `NANITE_MIN_VERTS` 미만은
   건드리지 않는다. 4정점 평면에 Nanite 를 켜면 클러스터 오버헤드만 생긴다.

**② 그림자 끄기** — **납작하거나 가려진 것**은 그림자를 만들 게 없는데
   마킹 비용만 낸다. 바닥 타일 182장이 대표적이다 (바닥에 누워 있다).
   벽 뒤 백플레인(`Grout_Unit`)도 영영 안 보인다.

⚠ 그림자를 끄는 것은 **레벨의 컴포넌트** 설정이라 이 레벨에만 적용된다.
  Nanite 는 **에셋** 설정이라 어디서 쓰든 따라간다.
"""
import unreal

LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
NANITE_MIN_VERTS = 200          # 이 미만은 Nanite 를 켜 봐야 손해다

# ⚠ 그림자를 만들 게 없는 것들 — 켜 두면 VSM 마킹 비용만 낸다
#
# ⚠⚠ **VSM 마킹 비용은 「정점 수」가 아니라 「바운즈가 덮는 넓이」에 붙는다.**
#   2026-08-28 에 실측한 non-Nanite 그림자 캐스터 넓이:
#
#       LL_Shell_Trans  80.53 m²   ← 실내 바닥(68 m²)보다 넓다!
#       Base_Unit ×8    11.52 m²
#       나머지 전부       0.61 m²
#
#   `LL_Shell_Trans` 는 정점이 6,339개뿐인데도 **유리·문·데칼이 방 곳곳에
#   흩어져 있어 바운딩 박스가 방 전체를 덮는다.** 셸을 반투명으로 가르면서
#   생긴 부작용이다 — 가르기 전보다 마킹 넓이가 오히려 커졌다.
NO_SHADOW = {
    'Tile_Floor',       # 4v 평면, 바닥에 누워 있다 (182장!)
    'Grout_Unit',       # 벽·바닥 뒤 백플레인 — 영영 안 보인다
    'Scasi_T', 'Scasi_E',   # 벽에 붙은 얇은 레터링
    'Mirror_Shared_1',      # 벽면 초타원 판
    # 유리·데칼 묶음. **유리는 원래 통짜 그림자를 던지면 안 된다** —
    # 반투명이 불투명 그림자를 만들면 그게 더 틀린 그림이다.
    # 다만 여기 든 출입문·카운터문의 불투명 부분도 같이 그림자를 잃는다.
    'LL_Shell_Trans',
    # 벽·기둥 밑 스테인리스 걸레받이. 바닥에 붙어 있어 그림자가 나올 게 없다
    'Base_Unit',
}

MESH_PATHS = ['/Game/BurgerHouse/Shell', '/Game/BurgerHouse/Props']


# ⚠ **반투명 마스터가 둘이다** (2026-08-28 굴절 분리). 하나만 적어 두면
#   굴절 마스터를 쓰는 유리가 있는 메쉬에 Nanite 가 켜져 §4 의 사고가
#   그대로 재발한다 — 반드시 둘 다.
TRANS_MASTERS = {'M_LL_Master_Trans', 'M_LL_Master_Refract'}


def has_translucent(m):
    """이 메쉬에 반투명 슬롯이 있는가.

    ⚠ **Nanite 는 반투명 머티리얼을 지원하지 않는다.** 켠 채로 반투명 슬롯이
      섞이면 그 메쉬 전체의 렌더가 망가진다 — 2026-08-28 에 `LL_Shell`(반투명
      슬롯 9개)에서 실제로 그랬고, 벽·거울까지 통째로 대각선으로 뭉개졌다.
      반사(Lumen)를 완전히 꺼도 증상이 남아서 그제서야 여기까지 좁혀졌다.
    """
    for sm in m.static_materials:
        mi = sm.material_interface
        if mi is None:
            continue
        try:
            par = mi.get_editor_property('parent')
        except Exception:
            par = None
        if (par.get_name() if par else mi.get_name()) in TRANS_MASTERS:
            return True
    return False


def set_nanite():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.search_all_assets(True)
    on, skip = [], []
    for base in MESH_PATHS:
        for a in ar.get_assets_by_path(base, recursive=True):
            m = unreal.load_asset(str(a.package_name))
            if not isinstance(m, unreal.StaticMesh):
                continue
            try:
                nv = m.get_num_vertices(0)
            except Exception:
                nv = 0
            if nv < NANITE_MIN_VERTS:
                skip.append((m.get_name(), nv))
                continue
            if has_translucent(m):
                # ⚠ 반투명이 섞인 메쉬는 **끈다** (위 설명 참조)
                st = m.get_editor_property('nanite_settings')
                st.set_editor_property('enabled', False)
                m.set_editor_property('nanite_settings', st)
                unreal.EditorAssetLibrary.save_asset(str(a.package_name))
                skip.append((m.get_name() + '(반투명)', nv))
                continue
            s = m.get_editor_property('nanite_settings')
            s.set_editor_property('enabled', True)
            m.set_editor_property('nanite_settings', s)
            unreal.EditorAssetLibrary.save_asset(str(a.package_name))
            on.append((m.get_name(), nv))
    unreal.log_warning('  Nanite 켬 %d개: %s'
                       % (len(on), [n for n, _ in sorted(on)]))
    unreal.log_warning('  건너뜀 %d개 (%d정점 미만): %s'
                       % (len(skip), NANITE_MIN_VERTS,
                          ['%s(%dv)' % t for t in sorted(skip)]))
    return on


def set_shadows():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)
    off = []
    # ⚠ **HISM 만 보면 안 된다.** `LL_Shell_Trans` 는 `StaticMeshActor` 라
    #   `HISM_` 접두사가 없어서 예전 코드는 그냥 지나쳤다 — 정작 마킹 넓이가
    #   가장 큰 놈인데.
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        asset = lbl[len('HISM_'):] if lbl.startswith('HISM_') else lbl
        if asset not in NO_SHADOW:
            continue
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            c.set_editor_property('cast_shadow', False)
            n = c.get_instance_count() if hasattr(c, 'get_instance_count') else 1
            off.append((asset, n))
    les.save_current_level()
    unreal.log_warning('  그림자 끔: %s'
                       % ['%s(%d개)' % t for t in sorted(off)])
    unreal.log_warning('  -> VSM 마킹 대상에서 인스턴스 %d개 제외'
                       % sum(n for _, n in off))


def main():
    unreal.log_warning('=== VSM 대응 ===')
    set_nanite()
    set_shadows()
    unreal.log_warning('=== 완료 — 레벨 저장됨 ===')
    unreal.log_warning('  경고가 남으면 그림자 끌 대상을 `NO_SHADOW` 에 더할 것.')
    unreal.log_warning('  ⚠ Nanite 는 **에셋** 설정이라 어디서 쓰든 따라가고,')
    unreal.log_warning('    그림자 끄기는 **이 레벨의 컴포넌트** 설정이다.')


main()
