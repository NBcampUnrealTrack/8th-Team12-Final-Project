"""NPC 들이 무슨 **애님 에셋 종류**로 움직이는지 훑는다. 안 고친다.

「애니메이션 시퀀스를 쓰나」는 한 겹 아래를 봐야 답이 나온다. 손님 하나만 해도
두 경로가 섞여 있다 —

    ① AnimGraph 의 상태 기계   -> AnimSequence 를 **직접** 재생
    ② PlayAction / 장식 루프     -> AnimMontage 를 슬롯에 재생 (몽타주 안에 시퀀스)

그래서 ABP 마다 **의존성을 클래스별로** 세고, 손님은 상태별로 따로 찍는다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

ANIM_CLASSES = ('AnimSequence', 'BlendSpace', 'BlendSpace1D', 'AimOffsetBlendSpace',
                'AimOffsetBlendSpace1D', 'AnimMontage', 'PoseAsset')

# NPC/캐릭터 BP -> ABP 는 idle 조사(`ll_idle_survey.py`)에서 나온 것
ABPS = [
    ('/Game/BurgerHouse/Anims/Mixamo/ABP_Customer', 'BurgerHouse 손님'),
    ('/Game/MetaHumans/Arin/Animations/ABP_Unarmed', 'BP_SubNpc · BP_FemaleBaseChar'),
    ('/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed', 'BP_MH_Suyun'),
    ('/Game/Characters/Character/Male/ABP_Male', 'BP_Doyun · BP_ThirdPersonCharacter'),
    ('/Game/Characters/Character/Male/ABP_CheonYul', 'BP_CheonYul'),
    ('/Game/Characters/Character/FeMale/ABP_Suah', 'BP_Suah'),
    ('/Game/MetaHumans/MHC_Character_Adam/Cheon/Anim/ABP_CheonYulNew', 'BP_CHeon'),
]


def classify(pkg):
    """패키지가 직접 참조하는 애님 에셋을 클래스별로 센다."""
    out = {}
    # ⚠ 이 판본은 `dependency_options` 가 **필수**다. 빼면 TypeError 로 조용히
    #   0개가 되어 「애님을 안 쓴다」는 엉뚱한 결론이 나온다.
    opts = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False)
    try:
        deps = REG.get_dependencies(pkg, opts) or []
    except Exception as ex:
        E('  의존성을 못 읽음: %s' % str(ex)[:90])
        return out
    for d in deps:
        for a in REG.get_assets_by_package_name(d):
            c = str(a.asset_class_path.asset_name)
            if c in ANIM_CLASSES:
                out.setdefault(c, []).append(str(a.asset_name))
    return out


L('=== ABP 별 애님 에셋 종류 ===')
for path, who in ABPS:
    if not EAL.does_asset_exist(path):
        E('  %-44s 없다' % path.replace('/Game/', ''))
        continue
    got = classify(path)
    L('')
    L('  %s' % path.replace('/Game/', ''))
    L('    쓰는 쪽: %s' % who)
    if not got:
        L('    (애님 에셋 직접 참조 없음)')
    for c in sorted(got):
        names = sorted(set(got[c]))
        L('    %-22s %2d개  %s' % (c, len(names),
                                  ', '.join(names[:6]) + (' …' if len(names) > 6 else '')))

# ---- 손님은 상태별로 ----
L('')
L('=== ABP_Customer 의 상태 기계 ===')
ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
for m in unreal.AnimGraphService.list_state_machines(ABP) or []:
    mn = ' '.join(str(m.machine_name).split())
    for s in unreal.AnimGraphService.list_states_in_machine(ABP, m.machine_name) or []:
        if str(s.state_type) != 'State':
            continue
        L('  [%s] %s' % (mn, ' '.join(str(s.state_name).split())))

L('')
L('  ⚠ 상태 기계가 쓰는 것은 **AnimSequence 직접 재생**이다 (Sequence Player 노드).')
L('    블렌드스페이스가 없다 — `Speed` 변수 하나뿐이고 `Direction` 이 없어서')
L('    어느 방향으로 움직이든 전진 애님만 나온다 (§9).')

L('')
L('=== 손님이 몽타주로 재생하는 것 (PlayAction / 장식 루프) ===')
cdo = unreal.get_default_object(
    unreal.load_object(None, '/Game/BurgerHouse/BP/BP_Customer.BP_Customer_C'))
actions = cdo.get_editor_property('actions')
seen = set()
for k in actions.keys():
    for v in actions[k].get_editor_property('variants'):
        mtg = v.get_editor_property('montage')
        if not mtg:
            continue
        src = None
        try:
            segs = (mtg.get_editor_property('slot_anim_tracks')[0]
                    .get_editor_property('anim_track')
                    .get_editor_property('anim_segments'))
            for sg in segs:
                for p in ('anim_reference', 'anim_sequence', 'animation'):
                    try:
                        x = sg.get_editor_property(p)
                        if x:
                            src = x
                            break
                    except Exception:
                        continue
                if src:
                    break
        except Exception:
            pass
        key = mtg.get_name()
        if key in seen:
            continue
        seen.add(key)
        L('  %-26s <- %s (%s)'
          % (key, src.get_name() if src else '?',
             src.get_class().get_name() if src else '?'))

L('')
L('  ⚠ 몽타주는 **AnimSequence 를 한 겹 감싼 것**이다. 재생은 DefaultSlot 으로')
L('    들어가고, ABP 의 Slot 노드가 상태 기계 포즈 위에 얹는다.')
