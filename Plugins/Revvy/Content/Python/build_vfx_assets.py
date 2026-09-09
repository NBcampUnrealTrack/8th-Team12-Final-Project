# 레비 VFX 표준 에셋 1회 구축 — 플러그인 내장 (/Revvy/VFX/...)
# ①TextureLibrary PNG 6종 임포트 ②마스터 머티리얼 2종 ③기본 MI 6종.
# 사용: execute_python_code 1회. 이미 있으면 각 단계 skip (재실행 안전).
import unreal
import os

PLUGIN_PY = os.path.dirname(os.path.abspath(__file__))
TEX_SRC = os.path.normpath(os.path.join(PLUGIN_PY, "..", "Skills", "unreal", "effect-generation", "TextureLibrary"))
TEX_DIR = "/Revvy/VFX/Textures"
MAT_DIR = "/Revvy/VFX/Materials"

ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
log = []

# ── ① 텍스처 임포트 (이름, sRGB) ──
TEXTURES = [
    ("T_SoftGlow", True), ("T_SparkStreak", True), ("T_SmokePuff", False),
    ("T_FireWisp", True), ("T_MagicRunic", True), ("T_LightningArc", True),
]

def import_textures():
    tasks = []
    for name, _srgb in TEXTURES:
        dst = f"{TEX_DIR}/{name}"
        if EAL.does_asset_exist(dst):
            log.append(f"tex exists: {name}")
            continue
        src = os.path.join(TEX_SRC, name + ".png")
        if not os.path.exists(src):
            log.append(f"tex MISSING SRC: {src}")
            continue
        t = unreal.AssetImportTask()
        t.filename = src
        t.destination_path = TEX_DIR
        t.destination_name = name
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    if tasks:
        ATOOLS.import_asset_tasks(tasks)
    for name, srgb in TEXTURES:
        tex = unreal.load_asset(f"{TEX_DIR}/{name}")
        if tex:
            tex.set_editor_property("srgb", srgb)
            tex.set_editor_property("never_stream", True)
            EAL.save_asset(f"{TEX_DIR}/{name}", only_if_is_dirty=False)
            log.append(f"tex OK: {name} (sRGB={'ON' if srgb else 'OFF'})")

# ── ② 마스터 머티리얼 ──
def new_material(name):
    path = f"{MAT_DIR}/{name}"
    if EAL.does_asset_exist(path):
        log.append(f"mat exists: {name}")
        return None
    return ATOOLS.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

def expr(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def temporal_envelope(mat, x, y):
    """Return a fast-attack, eased fade envelope driven by particle age."""
    age = expr(mat, unreal.MaterialExpressionParticleRelativeTime, x, y)
    attack = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        x,
        y + 120,
        parameter_name="AttackFraction",
        default_value=0.08,
    )
    fade = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        x,
        y + 240,
        parameter_name="FadeFraction",
        default_value=0.65,
    )
    attack_div = expr(mat, unreal.MaterialExpressionDivide, x + 190, y + 20)
    attack_sat = expr(mat, unreal.MaterialExpressionSaturate, x + 350, y + 20)
    remaining = expr(mat, unreal.MaterialExpressionOneMinus, x + 180, y + 190)
    fade_div = expr(mat, unreal.MaterialExpressionDivide, x + 350, y + 190)
    fade_sat = expr(mat, unreal.MaterialExpressionSaturate, x + 510, y + 190)
    envelope = expr(mat, unreal.MaterialExpressionMultiply, x + 680, y + 90)
    MEL.connect_material_expressions(age, "", attack_div, "A")
    MEL.connect_material_expressions(attack, "", attack_div, "B")
    MEL.connect_material_expressions(attack_div, "", attack_sat, "")
    MEL.connect_material_expressions(age, "", remaining, "")
    MEL.connect_material_expressions(remaining, "", fade_div, "A")
    MEL.connect_material_expressions(fade, "", fade_div, "B")
    MEL.connect_material_expressions(fade_div, "", fade_sat, "")
    MEL.connect_material_expressions(attack_sat, "", envelope, "A")
    MEL.connect_material_expressions(fade_sat, "", envelope, "B")
    return envelope, age


def build_polished_sprite_master():
    """Additive sprite master with an authored temporal envelope."""
    mat = new_material("M_RevvyVFX_PolishedSprite")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1300, -260, parameter_name="Texture")
    color = expr(mat, unreal.MaterialExpressionParticleColor, -1300, 20)
    tint = expr(
        mat,
        unreal.MaterialExpressionVectorParameter,
        -1300,
        250,
        parameter_name="Tint",
        default_value=unreal.LinearColor(1, 1, 1, 1),
    )
    intensity = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1300,
        450,
        parameter_name="EmissiveIntensity",
        default_value=1.5,
    )
    envelope, _ = temporal_envelope(mat, -1300, 650)
    m1 = expr(mat, unreal.MaterialExpressionMultiply, -950, -180)
    m2 = expr(mat, unreal.MaterialExpressionMultiply, -760, -100)
    m3 = expr(mat, unreal.MaterialExpressionMultiply, -570, -20)
    m4 = expr(mat, unreal.MaterialExpressionMultiply, -380, 60)
    MEL.connect_material_expressions(tex, "RGB", m1, "A")
    MEL.connect_material_expressions(color, "", m1, "B")
    MEL.connect_material_expressions(m1, "", m2, "A")
    MEL.connect_material_expressions(tint, "", m2, "B")
    MEL.connect_material_expressions(m2, "", m3, "A")
    MEL.connect_material_expressions(intensity, "", m3, "B")
    MEL.connect_material_expressions(m3, "", m4, "A")
    MEL.connect_material_expressions(envelope, "", m4, "B")
    MEL.connect_material_property(m4, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    o1 = expr(mat, unreal.MaterialExpressionMultiply, -540, 390)
    o2 = expr(mat, unreal.MaterialExpressionMultiply, -340, 410)
    MEL.connect_material_expressions(tex, "R", o1, "A")
    MEL.connect_material_expressions(color, "A", o1, "B")
    MEL.connect_material_expressions(o1, "", o2, "A")
    MEL.connect_material_expressions(envelope, "", o2, "B")
    MEL.connect_material_property(o2, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_RevvyVFX_PolishedSprite", only_if_is_dirty=False)
    log.append("master OK: M_RevvyVFX_PolishedSprite")


def build_polished_smoke_master():
    """Soft translucent secondary master with depth fade and age fade."""
    mat = new_material("M_RevvyVFX_PolishedSmoke")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1400, -260, parameter_name="Texture")
    color = expr(mat, unreal.MaterialExpressionParticleColor, -1400, 20)
    tint = expr(
        mat,
        unreal.MaterialExpressionVectorParameter,
        -1400,
        230,
        parameter_name="Tint",
        default_value=unreal.LinearColor(0.25, 0.2, 0.16, 1),
    )
    intensity = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1400,
        420,
        parameter_name="EmissiveIntensity",
        default_value=0.25,
    )
    opacity_scale = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1400,
        570,
        parameter_name="OpacityScale",
        default_value=0.45,
    )
    depth_distance = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1400,
        720,
        parameter_name="DepthFadeDistance",
        default_value=80.0,
    )
    envelope, _ = temporal_envelope(mat, -1400, 900)
    e1 = expr(mat, unreal.MaterialExpressionMultiply, -980, -180)
    e2 = expr(mat, unreal.MaterialExpressionMultiply, -790, -100)
    e3 = expr(mat, unreal.MaterialExpressionMultiply, -600, -20)
    e4 = expr(mat, unreal.MaterialExpressionMultiply, -410, 60)
    MEL.connect_material_expressions(tex, "RGB", e1, "A")
    MEL.connect_material_expressions(color, "", e1, "B")
    MEL.connect_material_expressions(e1, "", e2, "A")
    MEL.connect_material_expressions(tint, "", e2, "B")
    MEL.connect_material_expressions(e2, "", e3, "A")
    MEL.connect_material_expressions(intensity, "", e3, "B")
    MEL.connect_material_expressions(e3, "", e4, "A")
    MEL.connect_material_expressions(envelope, "", e4, "B")
    MEL.connect_material_property(e4, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    o1 = expr(mat, unreal.MaterialExpressionMultiply, -790, 420)
    o2 = expr(mat, unreal.MaterialExpressionMultiply, -600, 480)
    o3 = expr(mat, unreal.MaterialExpressionMultiply, -410, 520)
    depth = expr(mat, unreal.MaterialExpressionDepthFade, -600, 650)
    o4 = expr(mat, unreal.MaterialExpressionMultiply, -220, 560)
    MEL.connect_material_expressions(tex, "R", o1, "A")
    MEL.connect_material_expressions(color, "A", o1, "B")
    MEL.connect_material_expressions(o1, "", o2, "A")
    MEL.connect_material_expressions(opacity_scale, "", o2, "B")
    MEL.connect_material_expressions(o2, "", o3, "A")
    MEL.connect_material_expressions(envelope, "", o3, "B")
    MEL.connect_material_expressions(depth_distance, "", depth, "FadeDistance")
    MEL.connect_material_expressions(o3, "", o4, "A")
    MEL.connect_material_expressions(depth, "", o4, "B")
    MEL.connect_material_property(o4, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_RevvyVFX_PolishedSmoke", only_if_is_dirty=False)
    log.append("master OK: M_RevvyVFX_PolishedSmoke")


def build_polished_ring_master():
    """Expanding additive ring driven by particle age in World Position Offset."""
    mat = new_material("M_RevvyVFX_PolishedRing")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    color = expr(mat, unreal.MaterialExpressionParticleColor, -1350, -220)
    tint = expr(
        mat,
        unreal.MaterialExpressionVectorParameter,
        -1350,
        0,
        parameter_name="Tint",
        default_value=unreal.LinearColor(1, 0.35, 0.06, 1),
    )
    intensity = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1350,
        180,
        parameter_name="EmissiveIntensity",
        default_value=2.0,
    )
    fresnel_floor = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1350,
        340,
        parameter_name="FresnelFloor",
        default_value=0.2,
    )
    fresnel = expr(mat, unreal.MaterialExpressionFresnel, -1120, 350)
    fresnel_add = expr(mat, unreal.MaterialExpressionAdd, -920, 350)
    MEL.connect_material_expressions(fresnel, "", fresnel_add, "A")
    MEL.connect_material_expressions(fresnel_floor, "", fresnel_add, "B")
    envelope, age = temporal_envelope(mat, -1350, 520)
    e1 = expr(mat, unreal.MaterialExpressionMultiply, -900, -180)
    e2 = expr(mat, unreal.MaterialExpressionMultiply, -700, -110)
    e3 = expr(mat, unreal.MaterialExpressionMultiply, -500, -40)
    e4 = expr(mat, unreal.MaterialExpressionMultiply, -300, 30)
    MEL.connect_material_expressions(color, "", e1, "A")
    MEL.connect_material_expressions(tint, "", e1, "B")
    MEL.connect_material_expressions(e1, "", e2, "A")
    MEL.connect_material_expressions(intensity, "", e2, "B")
    MEL.connect_material_expressions(e2, "", e3, "A")
    MEL.connect_material_expressions(envelope, "", e3, "B")
    MEL.connect_material_expressions(e3, "", e4, "A")
    MEL.connect_material_expressions(fresnel_add, "", e4, "B")
    MEL.connect_material_property(e4, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = expr(mat, unreal.MaterialExpressionMultiply, -270, 250)
    opacity2 = expr(mat, unreal.MaterialExpressionMultiply, -90, 250)
    MEL.connect_material_expressions(color, "A", opacity, "A")
    MEL.connect_material_expressions(envelope, "", opacity, "B")
    MEL.connect_material_expressions(opacity, "", opacity2, "A")
    MEL.connect_material_expressions(fresnel_add, "", opacity2, "B")
    MEL.connect_material_property(opacity2, "", unreal.MaterialProperty.MP_OPACITY)
    world = expr(mat, unreal.MaterialExpressionWorldPosition, -900, 620)
    particle = expr(mat, unreal.MaterialExpressionParticlePositionWS, -900, 760)
    radial = expr(mat, unreal.MaterialExpressionSubtract, -700, 690)
    remaining = expr(mat, unreal.MaterialExpressionOneMinus, -900, 930)
    squared = expr(mat, unreal.MaterialExpressionMultiply, -700, 930)
    ease_out = expr(mat, unreal.MaterialExpressionOneMinus, -520, 930)
    expansion = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -700,
        1080,
        parameter_name="ExpansionAmount",
        default_value=1.6,
    )
    expansion_scale = expr(mat, unreal.MaterialExpressionMultiply, -330, 970)
    wpo = expr(mat, unreal.MaterialExpressionMultiply, -130, 760)
    MEL.connect_material_expressions(world, "", radial, "A")
    MEL.connect_material_expressions(particle, "", radial, "B")
    MEL.connect_material_expressions(age, "", remaining, "")
    MEL.connect_material_expressions(remaining, "", squared, "A")
    MEL.connect_material_expressions(remaining, "", squared, "B")
    MEL.connect_material_expressions(squared, "", ease_out, "")
    MEL.connect_material_expressions(ease_out, "", expansion_scale, "A")
    MEL.connect_material_expressions(expansion, "", expansion_scale, "B")
    MEL.connect_material_expressions(radial, "", wpo, "A")
    MEL.connect_material_expressions(expansion_scale, "", wpo, "B")
    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_RevvyVFX_PolishedRing", only_if_is_dirty=False)
    log.append("master OK: M_RevvyVFX_PolishedRing")


def build_polished_mesh_master():
    """Short-lived emissive shard material with temporal fade."""
    mat = new_material("M_RevvyVFX_PolishedMesh")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    color = expr(mat, unreal.MaterialExpressionParticleColor, -1000, -200)
    tint = expr(
        mat,
        unreal.MaterialExpressionVectorParameter,
        -1000,
        20,
        parameter_name="Tint",
        default_value=unreal.LinearColor(1, 0.25, 0.04, 1),
    )
    intensity = expr(
        mat,
        unreal.MaterialExpressionScalarParameter,
        -1000,
        200,
        parameter_name="EmissiveIntensity",
        default_value=1.5,
    )
    envelope, _ = temporal_envelope(mat, -1000, 390)
    fresnel = expr(mat, unreal.MaterialExpressionFresnel, -760, 720)
    m1 = expr(mat, unreal.MaterialExpressionMultiply, -620, -120)
    m2 = expr(mat, unreal.MaterialExpressionMultiply, -430, -40)
    m3 = expr(mat, unreal.MaterialExpressionMultiply, -240, 40)
    m4 = expr(mat, unreal.MaterialExpressionMultiply, -50, 120)
    MEL.connect_material_expressions(color, "", m1, "A")
    MEL.connect_material_expressions(tint, "", m1, "B")
    MEL.connect_material_expressions(m1, "", m2, "A")
    MEL.connect_material_expressions(intensity, "", m2, "B")
    MEL.connect_material_expressions(m2, "", m3, "A")
    MEL.connect_material_expressions(envelope, "", m3, "B")
    MEL.connect_material_expressions(m3, "", m4, "A")
    MEL.connect_material_expressions(fresnel, "", m4, "B")
    MEL.connect_material_property(m4, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = expr(mat, unreal.MaterialExpressionMultiply, -50, 330)
    MEL.connect_material_expressions(color, "A", opacity, "A")
    MEL.connect_material_expressions(envelope, "", opacity, "B")
    MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_RevvyVFX_PolishedMesh", only_if_is_dirty=False)
    log.append("master OK: M_RevvyVFX_PolishedMesh")

def build_master_additive():
    """M_LevyVFX_Additive — Texture × ParticleColor × Tint × EmissiveIntensity → Emissive.
    파라미터: Texture, Tint(Vector), EmissiveIntensity(Scalar, 2.0)"""
    mat = new_material("M_LevyVFX_Additive")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -200, parameter_name="Texture")
    pc = expr(mat, unreal.MaterialExpressionParticleColor, -900, 100)
    tint = expr(mat, unreal.MaterialExpressionVectorParameter, -900, 320,
                parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    inten = expr(mat, unreal.MaterialExpressionScalarParameter, -900, 540,
                 parameter_name="EmissiveIntensity", default_value=2.0)
    m1 = expr(mat, unreal.MaterialExpressionMultiply, -600, -50)
    m2 = expr(mat, unreal.MaterialExpressionMultiply, -420, 60)
    m3 = expr(mat, unreal.MaterialExpressionMultiply, -240, 160)
    MEL.connect_material_expressions(tex, "RGB", m1, "A")
    MEL.connect_material_expressions(pc, "", m1, "B")
    MEL.connect_material_expressions(m1, "", m2, "A")
    MEL.connect_material_expressions(tint, "", m2, "B")
    MEL.connect_material_expressions(m2, "", m3, "A")
    MEL.connect_material_expressions(inten, "", m3, "B")
    MEL.connect_material_property(m3, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(pc, "A", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_LevyVFX_Additive", only_if_is_dirty=False)
    log.append("master OK: M_LevyVFX_Additive")

def build_master_translucent():
    """M_LevyVFX_Translucent — 연기/먼지: gray Texture × Tint × ParticleColor → Emissive,
    Opacity = Texture.R × ParticleColor.A × OpacityScale × DepthFade(soft particle).
    파라미터: Texture, Tint, OpacityScale(0.8), DepthFadeDistance(60)"""
    mat = new_material("M_LevyVFX_Translucent")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -200, parameter_name="Texture")
    pc = expr(mat, unreal.MaterialExpressionParticleColor, -1100, 120)
    tint = expr(mat, unreal.MaterialExpressionVectorParameter, -1100, 360,
                parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    opa = expr(mat, unreal.MaterialExpressionScalarParameter, -1100, 560,
               parameter_name="OpacityScale", default_value=0.8)
    fade = expr(mat, unreal.MaterialExpressionScalarParameter, -1100, 700,
                parameter_name="DepthFadeDistance", default_value=60.0)
    # Emissive = tex.RGB × tint × pc.RGB
    e1 = expr(mat, unreal.MaterialExpressionMultiply, -760, -80)
    e2 = expr(mat, unreal.MaterialExpressionMultiply, -560, 0)
    MEL.connect_material_expressions(tex, "RGB", e1, "A")
    MEL.connect_material_expressions(tint, "", e1, "B")
    MEL.connect_material_expressions(e1, "", e2, "A")
    MEL.connect_material_expressions(pc, "", e2, "B")
    MEL.connect_material_property(e2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Opacity = tex.R × pc.A × OpacityScale × DepthFade
    o1 = expr(mat, unreal.MaterialExpressionMultiply, -760, 400)
    o2 = expr(mat, unreal.MaterialExpressionMultiply, -560, 480)
    df = expr(mat, unreal.MaterialExpressionDepthFade, -560, 640)
    o3 = expr(mat, unreal.MaterialExpressionMultiply, -360, 520)
    MEL.connect_material_expressions(tex, "R", o1, "A")
    MEL.connect_material_expressions(pc, "A", o1, "B")
    MEL.connect_material_expressions(o1, "", o2, "A")
    MEL.connect_material_expressions(opa, "", o2, "B")
    MEL.connect_material_expressions(fade, "", df, "FadeDistance")
    MEL.connect_material_expressions(o2, "", o3, "A")
    MEL.connect_material_expressions(df, "", o3, "B")
    MEL.connect_material_property(o3, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_LevyVFX_Translucent", only_if_is_dirty=False)
    log.append("master OK: M_LevyVFX_Translucent")

def build_master_erosion():
    """M_LevyVFX_Erosion — Xociety 프로덕션 표준 반영(Erosion/Panner가 최다 사용 파라미터).
    additive + 스크롤(Panner) 텍스처 + erosion 마스크 dissolve.
    파라미터: Texture, ErosionMask, Tint, EmissiveIntensity(2), ErosionAmount(0→1 사라짐),
              ErosionSoftness(0.2), PanSpeedX(0.1), PanSpeedY(0.0)"""
    mat = new_material("M_LevyVFX_Erosion")
    if not mat:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # 스크롤 메인 텍스처
    psx = expr(mat, unreal.MaterialExpressionScalarParameter, -1400, -340, parameter_name="PanSpeedX", default_value=0.1)
    psy = expr(mat, unreal.MaterialExpressionScalarParameter, -1400, -200, parameter_name="PanSpeedY", default_value=0.0)
    ap = expr(mat, unreal.MaterialExpressionAppendVector, -1220, -280)
    pan = expr(mat, unreal.MaterialExpressionPanner, -1060, -280)
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -880, -300, parameter_name="Texture")
    MEL.connect_material_expressions(psx, "", ap, "A")
    MEL.connect_material_expressions(psy, "", ap, "B")
    MEL.connect_material_expressions(ap, "", pan, "Speed")
    MEL.connect_material_expressions(pan, "", tex, "UVs")
    # erosion: saturate((Mask.R - ErosionAmount) / Softness)
    emask = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -880, 40, parameter_name="ErosionMask")
    eamt = expr(mat, unreal.MaterialExpressionScalarParameter, -880, 320, parameter_name="ErosionAmount", default_value=0.0)
    esoft = expr(mat, unreal.MaterialExpressionScalarParameter, -880, 460, parameter_name="ErosionSoftness", default_value=0.2)
    sub = expr(mat, unreal.MaterialExpressionSubtract, -640, 160)
    div = expr(mat, unreal.MaterialExpressionDivide, -500, 220)
    sat = expr(mat, unreal.MaterialExpressionSaturate, -380, 220)
    MEL.connect_material_expressions(emask, "R", sub, "A")
    MEL.connect_material_expressions(eamt, "", sub, "B")
    MEL.connect_material_expressions(sub, "", div, "A")
    MEL.connect_material_expressions(esoft, "", div, "B")
    MEL.connect_material_expressions(div, "", sat, "")
    # emissive = tex × ParticleColor × Tint × Intensity × erosion
    pc = expr(mat, unreal.MaterialExpressionParticleColor, -880, -80)
    tint = expr(mat, unreal.MaterialExpressionVectorParameter, -640, -120,
                parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    inten = expr(mat, unreal.MaterialExpressionScalarParameter, -640, -260,
                 parameter_name="EmissiveIntensity", default_value=2.0)
    m1 = expr(mat, unreal.MaterialExpressionMultiply, -460, -180)
    m2 = expr(mat, unreal.MaterialExpressionMultiply, -340, -100)
    m3 = expr(mat, unreal.MaterialExpressionMultiply, -220, -40)
    m4 = expr(mat, unreal.MaterialExpressionMultiply, -100, 40)
    MEL.connect_material_expressions(tex, "RGB", m1, "A")
    MEL.connect_material_expressions(pc, "", m1, "B")
    MEL.connect_material_expressions(m1, "", m2, "A")
    MEL.connect_material_expressions(tint, "", m2, "B")
    MEL.connect_material_expressions(m2, "", m3, "A")
    MEL.connect_material_expressions(inten, "", m3, "B")
    MEL.connect_material_expressions(m3, "", m4, "A")
    MEL.connect_material_expressions(sat, "", m4, "B")
    MEL.connect_material_property(m4, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = expr(mat, unreal.MaterialExpressionMultiply, -100, 280)
    MEL.connect_material_expressions(pc, "A", opacity, "A")
    MEL.connect_material_expressions(sat, "", opacity, "B")
    MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/M_LevyVFX_Erosion", only_if_is_dirty=False)
    log.append("master OK: M_LevyVFX_Erosion")

# ── ③ 기본 MI 6종 (텍스처 ↔ 마스터 매핑) ──
MIS = [
    ("MI_LevyGlow",      "M_LevyVFX_Additive",    "T_SoftGlow"),
    ("MI_LevySpark",     "M_LevyVFX_Additive",    "T_SparkStreak"),
    ("MI_LevyFireWisp",  "M_LevyVFX_Additive",    "T_FireWisp"),
    ("MI_LevyRunic",     "M_LevyVFX_Additive",    "T_MagicRunic"),
    ("MI_LevyLightning", "M_LevyVFX_Additive",    "T_LightningArc"),
    ("MI_LevySmoke",     "M_LevyVFX_Translucent", "T_SmokePuff"),
]

POLISHED_MIS = [
    (
        "MI_RevvyPolishedGlow",
        "M_RevvyVFX_PolishedSprite",
        "T_SoftGlow",
        unreal.LinearColor(1.0, 0.55, 0.16, 1.0),
        {"EmissiveIntensity": 1.4, "AttackFraction": 0.05, "FadeFraction": 0.72},
    ),
    (
        "MI_RevvyPolishedSpark",
        "M_RevvyVFX_PolishedSprite",
        "T_SparkStreak",
        unreal.LinearColor(1.0, 0.28, 0.04, 1.0),
        {"EmissiveIntensity": 1.8, "AttackFraction": 0.03, "FadeFraction": 0.82},
    ),
    (
        "MI_RevvyPolishedSmoke",
        "M_RevvyVFX_PolishedSmoke",
        "T_SmokePuff",
        unreal.LinearColor(0.22, 0.16, 0.12, 1.0),
        {"EmissiveIntensity": 0.18, "OpacityScale": 0.36, "AttackFraction": 0.12, "FadeFraction": 0.78},
    ),
    (
        "MI_RevvyPolishedDarkCore",
        "M_RevvyVFX_PolishedSmoke",
        "T_SoftGlow",
        unreal.LinearColor(0.004, 0.002, 0.008, 1.0),
        {"EmissiveIntensity": 0.0, "OpacityScale": 0.72, "AttackFraction": 0.03, "FadeFraction": 0.72},
    ),
    (
        "MI_RevvyPolishedRing",
        "M_RevvyVFX_PolishedRing",
        None,
        unreal.LinearColor(1.0, 0.26, 0.035, 1.0),
        {"EmissiveIntensity": 2.1, "ExpansionAmount": 1.9, "AttackFraction": 0.04, "FadeFraction": 0.7},
    ),
    (
        "MI_RevvyPolishedShard",
        "M_RevvyVFX_PolishedMesh",
        None,
        unreal.LinearColor(0.8, 0.16, 0.025, 1.0),
        {"EmissiveIntensity": 1.35, "AttackFraction": 0.03, "FadeFraction": 0.76},
    ),
    (
        "MI_RevvyPolishedRunic",
        "M_RevvyVFX_PolishedSprite",
        "T_MagicRunic",
        unreal.LinearColor(0.12, 0.5, 1.0, 1.0),
        {"EmissiveIntensity": 2.4, "AttackFraction": 0.08, "FadeFraction": 0.7},
    ),
    (
        "MI_RevvyPolishedArcaneSpark",
        "M_RevvyVFX_PolishedSprite",
        "T_SparkStreak",
        unreal.LinearColor(0.12, 0.52, 1.0, 1.0),
        {"EmissiveIntensity": 2.4, "AttackFraction": 0.03, "FadeFraction": 0.82},
    ),
    (
        "MI_RevvyPolishedArcaneSmoke",
        "M_RevvyVFX_PolishedSmoke",
        "T_SmokePuff",
        unreal.LinearColor(0.08, 0.25, 0.72, 1.0),
        {"EmissiveIntensity": 0.42, "OpacityScale": 0.32, "AttackFraction": 0.12, "FadeFraction": 0.82},
    ),
    (
        "MI_RevvyTutorial_Core",
        "M_RevvyVFX_PolishedSprite",
        "T_SoftGlow",
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 2.2, "AttackFraction": 0.04, "FadeFraction": 0.76},
    ),
    (
        "MI_RevvyTutorial_Streak",
        "M_RevvyVFX_PolishedSprite",
        "T_SparkStreak",
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 2.4, "AttackFraction": 0.025, "FadeFraction": 0.84},
    ),
    (
        "MI_RevvyTutorial_Wisp",
        "M_RevvyVFX_PolishedSprite",
        "T_FireWisp",
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 1.7, "AttackFraction": 0.08, "FadeFraction": 0.82},
    ),
    (
        "MI_RevvyTutorial_Rune",
        "M_RevvyVFX_PolishedSprite",
        "T_MagicRunic",
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 2.4, "AttackFraction": 0.08, "FadeFraction": 0.74},
    ),
    (
        "MI_RevvyTutorial_Smoke",
        "M_RevvyVFX_PolishedSmoke",
        "T_SmokePuff",
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 0.22, "OpacityScale": 0.35, "AttackFraction": 0.12, "FadeFraction": 0.82},
    ),
    (
        "MI_RevvyTutorial_Ring",
        "M_RevvyVFX_PolishedRing",
        None,
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 2.2, "ExpansionAmount": 0.32, "AttackFraction": 0.04, "FadeFraction": 0.76},
    ),
    (
        "MI_RevvyTutorial_Mesh",
        "M_RevvyVFX_PolishedMesh",
        None,
        unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        {"EmissiveIntensity": 1.6, "AttackFraction": 0.03, "FadeFraction": 0.78},
    ),
]

def build_instances():
    for mi_name, master_name, tex_name in MIS:
        path = f"{MAT_DIR}/{mi_name}"
        if EAL.does_asset_exist(path):
            log.append(f"mi exists: {mi_name}")
            continue
        master = unreal.load_asset(f"{MAT_DIR}/{master_name}")
        tex = unreal.load_asset(f"{TEX_DIR}/{tex_name}")
        if not master or not tex:
            log.append(f"mi SKIP (deps missing): {mi_name}")
            continue
        mi = ATOOLS.create_asset(mi_name, MAT_DIR, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", master)
        MEL.set_material_instance_texture_parameter_value(mi, "Texture", tex)
        EAL.save_asset(path, only_if_is_dirty=False)
        log.append(f"mi OK: {mi_name} ({master_name} + {tex_name})")

    for mi_name, master_name, tex_name, tint, scalars in POLISHED_MIS:
        path = f"{MAT_DIR}/{mi_name}"
        master = unreal.load_asset(f"{MAT_DIR}/{master_name}")
        texture = unreal.load_asset(f"{TEX_DIR}/{tex_name}") if tex_name else None
        if not master or (tex_name and not texture):
            log.append(f"polished mi SKIP (deps missing): {mi_name}")
            continue
        mi = unreal.load_asset(path)
        if not mi:
            mi = ATOOLS.create_asset(
                mi_name,
                MAT_DIR,
                unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew(),
            )
        if not mi:
            log.append(f"polished mi FAIL: {mi_name}")
            continue
        mi.set_editor_property("parent", master)
        if texture:
            MEL.set_material_instance_texture_parameter_value(mi, "Texture", texture)
        MEL.set_material_instance_vector_parameter_value(mi, "Tint", tint)
        for parameter_name, value in scalars.items():
            MEL.set_material_instance_scalar_parameter_value(mi, parameter_name, float(value))
        MEL.update_material_instance(mi)
        EAL.save_asset(path, only_if_is_dirty=False)
        log.append(f"polished mi OK: {mi_name}")

def patch_niagara_usage_flags():
    """마스터에 Niagara usage 플래그 — 없으면 Niagara 렌더러에서 기본(박스) 머티리얼로 fallback!
    기존 에셋에도 재실행 안전 (이미 켜져 있으면 no-op 후 저장 skip)."""
    for name in (
        "M_LevyVFX_Additive",
        "M_LevyVFX_Translucent",
        "M_LevyVFX_Erosion",
        "M_RevvyVFX_PolishedSprite",
        "M_RevvyVFX_PolishedSmoke",
        "M_RevvyVFX_PolishedRing",
        "M_RevvyVFX_PolishedMesh",
    ):
        path = f"{MAT_DIR}/{name}"
        mat = unreal.load_asset(path)
        if not mat:
            log.append(f"usage-flag SKIP (missing): {name}")
            continue
        changed = False
        for prop in ("used_with_niagara_sprites", "used_with_niagara_ribbons", "used_with_niagara_mesh_particles", "used_with_particle_sprites"):
            try:
                if not mat.get_editor_property(prop):
                    mat.set_editor_property(prop, True)
                    changed = True
            except Exception:
                pass
        if changed:
            MEL.recompile_material(mat)
            EAL.save_asset(path, only_if_is_dirty=False)
            log.append(f"usage flags ON: {name}")
        else:
            log.append(f"usage flags ok: {name}")


def patch_particle_alpha_contract():
    """Make ParticleColor.A control additive lifetime fade on existing masters."""
    for name in ("M_LevyVFX_Additive", "M_LevyVFX_Erosion"):
        path = f"{MAT_DIR}/{name}"
        mat = unreal.load_asset(path)
        if not mat:
            log.append(f"alpha contract SKIP (missing): {name}")
            continue
        existing = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_OPACITY)
        if existing:
            log.append(f"alpha contract ok: {name}")
            continue
        particle_color = expr(mat, unreal.MaterialExpressionParticleColor, -140, 520)
        MEL.connect_material_property(particle_color, "A", unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(mat)
        EAL.save_asset(path, only_if_is_dirty=False)
        log.append(f"alpha contract added: {name}")

import_textures()
build_master_additive()
build_master_translucent()
build_master_erosion()
build_polished_sprite_master()
build_polished_smoke_master()
build_polished_ring_master()
build_polished_mesh_master()
patch_niagara_usage_flags()
patch_particle_alpha_contract()
build_instances()
print("\n".join(log))
