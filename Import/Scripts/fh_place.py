"""FlowerHouse 레벨 — `FH_placement.json` 배치를 새 맵 `FlowerHouse.umap` 에 깐다

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script="C:/Project/Unreal/Import/Scripts/fh_place.py" -unattended -nopause -nosplash

레벨: `/Game/Levels/FlowerHouse/FlowerHouse`  (fh_import.py 를 먼저 돌릴 것)

## 배치

`ll_place.py` 와 같은 원칙 — 같은 메쉬는 **HISM 하나**에 모은다(2개 이상일 때).
한 번만 쓰이는 메쉬(셸·벽·테이블 등)는 `StaticMeshActor`. 전부
아웃라이너 폴더 `FlowerHouse/...` 아래에 들어간다.

## 좌표 — 팀 규약 그대로 (ll_place.py AXIS)

    UE 위치 = (x, −y, z) × 100
    회전은 JSON 의 쿼터니언 (w, x, y, z) 을 (w, −x, y, −z) 로 옮긴다 —
    같은 Y 반전이다. 한 축씩 풀면 ll_place 의 Roll +rx · Pitch −ry · Yaw −rz
    와 정확히 같고, 90° 로 눕힌 책처럼 짐벌이 걸리는 자세에서도 안전하다.

## 조명

블렌더 파일은 조명 없이 넘어온다(언리얼에서 다시 짓기로 한 규칙).
레일 스폿 16개(`FH_lights.json`, 캔이 비추는 방향 그대로) + 하늘/해/안개와
플레이어 스타트를 기본으로 깐다. 다시 돌리면 `FH_` 로 시작하는 액터만
지우고 새로 만든다 — 손으로 추가한 액터는 건드리지 않는다.

## 저장 — BURGERHOUSE.md §6

`save_current_level()` 의 True 는 믿지 않는다. 월드 패키지를
`save_packages(..., only_dirty=False)` 로 저장하고, 검산은 새 프로세스로.
"""
import unreal
import json
import os
from collections import defaultdict

IMPORT_DIR = r'C:/Project/Unreal/Import'
J_PLACE = IMPORT_DIR + '/FH_placement.json'
J_LIGHTS = IMPORT_DIR + '/FH_lights.json'
J_MATS = IMPORT_DIR + '/FH_materials.json'
MESH_DEST = '/Game/Levels/FlowerHouse/Scene/Meshes/'
LEVEL = '/Game/Levels/FlowerHouse/FlowerHouse'
M = 100.0
PREFIX = 'FH_'
# 조명 스펙 — 버거집(`ll_lights.py`)을 따른다: 루멘 단위 · 색온도 · **Movable**
#
# ⚠⚠ 1차(2026-09-30)는 모빌리티를 안 줘서 **Stationary** 로 들어갔다. 스폿 16개가
#   한 방에 겹쳐 「겹친 Stationary 4개 초과」(아이콘에 붉은 X) 가 되었고, 사용자가
#   「라이트가 작동하지 않는다」고 했다. 이 프로젝트는 Lumen 이라 라이트맵을 굽지
#   않는다 -> 버거집과 같이 **전부 Movable**.
#
#   밝기: 버거집 홀 13,303 lm / 68 m² ≈ 196 lux 상한. 이 매장 실내 55 m² 에
#   같은 밀도 ≈ 10,800 lm. 1차는 스폿 450 × 16 = 7,200 lm 뿐이었다.
#   색: 사용자가 「지금 정도의 따뜻함」을 원해 **3000K** 를 지킨다
#   (버거집은 3500/4200K). 쇼케이스 안만 냉장 진열장답게 약간 중성(3300K).
#   원뿔은 버거집 레일과 같이 30°/45°(반각) — 좁으면 바닥·테이블에 웅덩이가 찍힌다.
#   감쇠는 「빛이 닿는 곳」이 아니라 VSM 이 세는 범위라 버거집 수준으로 짧게.
# ⚠ 2차 (2026-10-01) — 「백룸 같다」: 전부 3000K 에 보조광이 고르게 덮어
#   누런 단색·무명암이 됐다. 일반적인 매장 사진 쪽으로 —
#     레일 3500K(따뜻한 악센트) · 보조광 4000K 중성으로 낮게 · 쇼케이스 4500K
#     (냉장 진열장) · 낮의 채광은 해가 앞 유리로 들어오게 (SUN_LUX)
LIGHT_SPEC = {
    'track':    {'lumens': 380, 'kelvin': 3500, 'atten': 380, 'inner': 30, 'outer': 45, 'src': 2.0},
    'fill':     {'lumens': 350, 'kelvin': 4000, 'atten': 420, 'src': 25.0},   # 3차: 250 은 어두웠다
    'showcase': {'lumens': 600, 'kelvin': 4500, 'atten': 260, 'shadow': False},
    'strip':    {'lumens': 300, 'kelvin': 3500, 'atten': 200, 'shadow': False},
}
# 낮의 해 — 앞(UE +Y, 거리 쪽)에서 유리 너머 매장 안으로 비스듬히. 10 lux(기본)는
# 실내 조명(≈150 lux)에 묻혀 채광이 0 이었다.
SUN_LUX = 8000.0
SUN_ROT = dict(roll=0.0, pitch=-30.0, yaw=-105.0)
# 노출 — 보정 0, 자동 적응 범위만 준다 (EV100). 실내 매장 ≈ EV 6~8.
EXPOSURE = dict(bias=1.0, min_ev=5.0, max_ev=10.0)   # 3차: 보정 0 은 「아직 어둡다」 -> +1 스톱
# 천장 보조광(Point) — 블렌더의 천장 면광원 자리. 레일(x −0.9 / +1.4) 사이 통로
# 위와 오른쪽 선반벽 위에 3개씩, 천장 25cm 아래 (블렌더 좌표, m)
FILL_POINTS = [(0.25, 2.8), (0.25, 5.4), (0.25, 8.0), (2.3, 4.2), (2.3, 6.8), (2.3, 9.4)]
FILL_Z = 2.25
NO_SHADOW = ('FH_Leaf', 'FH_StemCut', 'Floor_Tiles', 'Floor_Bed', 'FH_Poster',
             'FH_PhotoPrint', 'FH_Card', 'FH_TissueRose', 'FH_TissueCream', 'FH_KraftSheet')


def log(msg):
    unreal.log_warning('[FH] ' + msg)


def to_loc(p):
    return unreal.Vector(p[0] * M, -p[1] * M, p[2] * M)


def to_quat(q):
    w, x, y, z = q
    return unreal.Quat(-x, y, -z, w)


def to_xform(it):
    t = unreal.Transform()
    t.translation = to_loc(it['loc'])
    t.rotation = to_quat(it['quat'])
    s = it['scale']
    t.scale3d = unreal.Vector(s[0], s[1], s[2])
    return t


def add_hism(eas, sds, label, mesh):
    a = eas.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    a.set_actor_label(label)
    root = sds.k2_gather_subobject_data_for_instance(a)[0]
    p = unreal.AddNewSubobjectParams()
    p.set_editor_property('parent_handle', root)
    p.set_editor_property('new_class', unreal.HierarchicalInstancedStaticMeshComponent)
    h, fail = sds.add_new_subobject(p)
    if fail and str(fail):
        unreal.log_error('[FH] %s HISM 추가 실패: %s' % (label, fail))
        return a, None
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(
        sds.k2_find_subobject_data_from_handle(h))
    comp.set_static_mesh(mesh)
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    return a, comp


def spawn(eas, cls, label, loc=(0, 0, 0), rot=None, folder='FlowerHouse/Env'):
    a = eas.spawn_actor_from_class(cls, unreal.Vector(*loc))
    a.set_actor_label(label)
    if rot is not None:
        a.set_actor_rotation(rot, False)
    a.set_folder_path(folder)
    return a


def main():
    with open(J_PLACE, encoding='utf-8') as f:
        d = json.load(f)
    lights = {}
    if os.path.exists(J_LIGHTS):
        with open(J_LIGHTS, encoding='utf-8') as f:
            lights = json.load(f)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        les.load_level(LEVEL)
        old = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith(PREFIX)]
        for a in old:
            eas.destroy_actor(a)
        log('기존 레벨 로드 / FH_ 액터 %d개 제거' % len(old))
    else:
        les.new_level(LEVEL)
        log('새 레벨: %s' % LEVEL)

    # ⚠⚠ GPU 크래시 (2026-09-30, DXGI_ERROR_DEVICE_HUNG / TDR) — Aftermath 가
    #   짚은 셰이더는 LumenTranslucencyVolumeHardwareRayTracingCS 하나였다.
    #   이 맵에는 **알파 마스크 잎이 442개**(220만 tris) 겹겹이 있고, HWRT 는
    #   광선이 잎에 닿을 때마다 any-hit 으로 알파를 읽는다. 노트북 5060 에서
    #   한 디스패치가 2초(TDR)를 넘겼다. 버거집엔 이런 잎이 거의 없다.
    #   -> 마스크·반투명 슬롯이 있는 메쉬는 **레이트레이싱에서 뺀다.** Lumen 은
    #      그것들을 거리장(소프트웨어)으로 다룬다. 이 맵에만 걸리는 설정이라
    #      팀 공용 DefaultEngine.ini 는 건드리지 않는다.
    kinds = {}
    if os.path.exists(J_MATS):
        with open(J_MATS, encoding='utf-8') as f:
            kinds = {k: v.get('kind', 'opaque') for k, v in json.load(f)['materials'].items()}

    def no_rt(asset):
        mats = d['assets'].get(asset, {}).get('mats', [])
        return any(kinds.get(m) in ('masked', 'translucent') for m in mats)

    n_nort = 0
    groups = defaultdict(list)
    for it in d['instances']:
        groups[it['asset']].append(it)
    n_hism = n_sma = n_inst = 0
    miss = []
    for asset in sorted(groups):
        mesh = unreal.load_asset(MESH_DEST + asset)
        if mesh is None:
            miss.append(asset)
            continue
        items = groups[asset]
        noshadow = any(k in asset for k in NO_SHADOW)
        skip_rt = no_rt(asset)
        n_nort += len(items) if skip_rt else 0
        if len(items) >= 2:
            a, comp = add_hism(eas, sds, PREFIX + 'HISM_' + asset[6:], mesh)
            if comp is None:
                continue
            a.set_folder_path('FlowerHouse/Instanced')
            for it in items:
                comp.add_instance(to_xform(it))
            if noshadow:
                comp.set_editor_property('cast_shadow', False)
            if skip_rt:
                comp.set_editor_property('visible_in_ray_tracing', False)
            n_hism += 1
            n_inst += len(items)
        else:
            it = items[0]
            a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
            a.set_actor_label(PREFIX + asset[6:])
            smc = a.static_mesh_component
            smc.set_static_mesh(mesh)
            a.set_actor_transform(to_xform(it), False, False)
            smc.set_mobility(unreal.ComponentMobility.STATIC)
            if noshadow:
                smc.set_editor_property('cast_shadow', False)
            if skip_rt:
                smc.set_editor_property('visible_in_ray_tracing', False)
            a.set_folder_path('FlowerHouse/Static')
            n_sma += 1
            n_inst += 1
    log('배치: HISM %d개 + 단일 액터 %d개 = 인스턴스 %d (에셋 없음 %d %s)'
        % (n_hism, n_sma, n_inst, len(miss), miss[:5]))
    log('레이트레이싱 제외(마스크·반투명): 인스턴스 %d개' % n_nort)

    # ---- 조명 ----
    ml = unreal.MathLibrary
    DOWN = unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0)
    made = {}

    def light(cls, kind, label, loc_ue, rot):
        sp = LIGHT_SPEC[kind]
        a = spawn(eas, cls, label, (0, 0, 0), folder='FlowerHouse/Lights')
        a.set_actor_location(loc_ue, False, False)
        a.set_actor_rotation(rot, False)
        c = a.light_component
        # ⚠ Movable — Stationary 는 겹치면 꺼지고(붉은 X) 라이트맵을 기다린다
        c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_editor_property('intensity_units', unreal.LightUnits.LUMENS)
        c.set_editor_property('intensity', float(sp['lumens']))
        c.set_editor_property('use_temperature', True)
        c.set_editor_property('temperature', float(sp['kelvin']))
        c.set_editor_property('attenuation_radius', float(sp['atten']))
        c.set_editor_property('cast_shadows', bool(sp.get('shadow', True)))
        made.setdefault(kind, []).append(a)
        return c

    for k, s in enumerate(lights.get('spots', [])):          # 레일 스폿
        dv = s['dir']
        c = light(unreal.SpotLight, 'track', '%sSpot_%02d' % (PREFIX, k), to_loc(s['loc']),
                  ml.make_rot_from_x(unreal.Vector(dv[0], -dv[1], dv[2])))
        c.set_editor_property('inner_cone_angle', float(LIGHT_SPEC['track']['inner']))
        c.set_editor_property('outer_cone_angle', float(LIGHT_SPEC['track']['outer']))
        c.set_editor_property('source_radius', float(LIGHT_SPEC['track']['src']))
    for k, (x, y) in enumerate(FILL_POINTS):                 # 천장 보조광
        c = light(unreal.PointLight, 'fill', '%sFill_%02d' % (PREFIX, k),
                  to_loc((x, y, FILL_Z)), unreal.Rotator(0, 0, 0))
        c.set_editor_property('source_radius', float(LIGHT_SPEC['fill']['src']))
    for k, b in enumerate(lights.get('showcases', [])):      # 쇼케이스 안 LED 띠
        lo, hi = b['lo'], b['hi']
        cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
        c = light(unreal.RectLight, 'showcase', '%sShowcase_%d' % (PREFIX, k + 1),
                  to_loc((cx, cy, hi[2] - 0.30)), DOWN)
        # 피치 −90: 로컬 Y -> 월드 Y (width), 로컬 Z -> 월드 X (height) — ll_lights
        c.set_editor_property('source_width', (hi[1] - lo[1] - 0.2) * M)
        c.set_editor_property('source_height', max((hi[0] - lo[0]) * 0.5 * M, 5.0))
    for k, b in enumerate(lights.get('strips', [])):         # DP 선반 LED
        lo, hi = b['lo'], b['hi']
        c = light(unreal.RectLight, 'strip', '%sShelfLED_%d' % (PREFIX, k + 1),
                  to_loc(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2] - 0.005)), DOWN)
        c.set_editor_property('source_width', (hi[1] - lo[1]) * M)
        c.set_editor_property('source_height', 5.0)
    tot = 0
    for kind in ('track', 'fill', 'showcase', 'strip'):
        n = len(made.get(kind, []))
        lm = LIGHT_SPEC[kind]['lumens'] * n
        tot += lm
        log('조명 %-9s %2d등 × %d lm = %5d lm  %dK' % (kind, n, LIGHT_SPEC[kind]['lumens'], lm,
                                                   LIGHT_SPEC[kind]['kelvin']))
    log('조명 합계 %d lm (실내 55 m² 기준 약 %d lux 상한, 버거집 ≈196)' % (tot, tot / 55.0))
    sun = spawn(eas, unreal.DirectionalLight, PREFIX + 'Sun', (0, 0, 800),
                unreal.Rotator(**SUN_ROT))
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property('atmosphere_sun_light', True)
    sc.set_editor_property('intensity', SUN_LUX)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)     # 굽지 않는다 (Lumen)
    spawn(eas, unreal.SkyAtmosphere, PREFIX + 'SkyAtmosphere')
    sky = spawn(eas, unreal.SkyLight, PREFIX + 'SkyLight', (0, 0, 600))
    slc = sky.get_component_by_class(unreal.SkyLightComponent)
    slc.set_editor_property('real_time_capture', True)
    slc.set_mobility(unreal.ComponentMobility.MOVABLE)
    spawn(eas, unreal.ExponentialHeightFog, PREFIX + 'HeightFog', (0, 0, -100))
    # ⚠⚠ 노출 — 버거집 `L_BurgerHouse` 의 `LL_Light_PPV`(에디터에서 손으로 만든
    #   무경계 PPV) 값을 그대로 옮긴다. 프로젝트 기본은 AutoExposure=False 라
    #   이 볼륨이 없으면 노출이 한 값에 묶인다 — 1차엔 어둡다는 말을 들었고,
    #   라이트를 올리자 하얗게 날아갔다. 0.6~2.0 사이에서 적응 · 보정 +0.7.
    ppv = spawn(eas, unreal.PostProcessVolume, PREFIX + 'PPV', (0, -600, 150))
    ppv.set_editor_property('unbound', True)
    ps = ppv.get_editor_property('settings')
    # (2차: 버거집 값 +0.7 / 0.6~2.0 은 이 매장을 한 단계 들뜨게 했다 -> EXPOSURE)
    for k, v in (('override_auto_exposure_bias', True), ('auto_exposure_bias', EXPOSURE['bias']),
                 ('override_auto_exposure_min_brightness', True), ('auto_exposure_min_brightness', EXPOSURE['min_ev']),
                 ('override_auto_exposure_max_brightness', True), ('auto_exposure_max_brightness', EXPOSURE['max_ev'])):
        ps.set_editor_property(k, v)
    ppv.set_editor_property('settings', ps)
    # 문 안쪽에서 매장 안쪽(블렌더 +Y = UE −Y)을 보고 선다
    spawn(eas, unreal.PlayerStart, PREFIX + 'PlayerStart', (-12.0, -200.0, 100.0),
          unreal.Rotator(roll=0, pitch=0, yaw=-90), folder='FlowerHouse')

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ok = unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False)
    log('레벨 저장 %s: %s' % ('OK' if ok else '⚠ 실패', LEVEL))


main()
