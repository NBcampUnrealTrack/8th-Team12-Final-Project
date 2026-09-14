# 이미터 팔레트 1회 구축 — FASTBUILD.md 표준 블록 6종.
# 사용: 레비가 execute_python_code로 실행 (팔레트 없을 때 1회).
# 결과: /Revvy/VFX/_Library/NS_EmitterPalette 에 EM_* 블록 6종 (플러그인 내장).
import unreal

PALETTE_DIR = "/Revvy/VFX/_Library"
PALETTE = PALETTE_DIR + "/NS_EmitterPalette"

# NIAGARA_5_7.md 카탈로그 경로 (V2 필수 모듈은 V2)
M = {
    "init":   "/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle",
    "rate":   "/Niagara/Modules/Emitter/SpawnRate",
    "burst":  "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous",
    "velpt":  "/Niagara/Modules/Spawn/Velocity/AddVelocityFromPoint",
    "vel":    "/Niagara/Modules/Spawn/Velocity/AddVelocity",
    "grav":   "/Niagara/Modules/Update/Forces/GravityForce",
    "drag":   "/Niagara/Modules/Update/Forces/Drag",
    "curl":   "/Niagara/Modules/Update/Forces/V2/CurlNoiseForce",
    "solve":  "/Niagara/Modules/Solvers/SolveForcesAndVelocity",
    "scale_color": "/Niagara/Modules/Update/Color/ScaleColor",
    "scale_sprite": "/Niagara/Modules/Update/Size/ScaleSpriteSize",
    "scale_mesh": "/Niagara/Modules/Update/Size/ScaleMeshSize",
    "dynamic_material": "/Niagara/Modules/Update/Material/DynamicMaterialParameters",
    "init_ribbon": "/Niagara/Modules/Spawn/Initialization/InitializeRibbon",
    "scale_ribbon": "/Niagara/Modules/Ribbons/ScaleRibbonWidth",
}

NS = unreal.NiagaraService
ES = unreal.NiagaraEmitterService
log = []

BLOCK_RENDERERS = {
    "EM_BurstSparks": "Sprite",
    "EM_ExpandingRing": "Mesh",
    "EM_RisingWisps": "Sprite",
    "EM_GlowCore": "Sprite",
    "EM_GroundDust": "Sprite",
    "EM_ElectricArc": "Ribbon",
    "EM_MeshDebris": "Mesh",
    "EM_RibbonTrail": "Ribbon",
}

def ensure_system():
    # load_asset 직접 시도 — does_asset_exist는 커맨드렛 초기에 asset registry
    # 미스캔으로 false negative를 낸다 (이미 있는데 create 시도 → 실패).
    if unreal.load_asset(PALETTE):
        log.append(f"palette exists: {PALETTE}")
        return True
    # NiagaraService.create_system은 폴더에 /Game을 prepend하므로 /Revvy(플러그인) 경로엔
    # 못 쓴다(더블 슬래시 크래시). AssetTools로 직접 생성.
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = atools.create_asset("NS_EmitterPalette", PALETTE_DIR,
                                unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
    ok = asset is not None
    log.append(f"create palette: {ok}")
    return ok

def block(name, modules, renderer):
    """modules = [(asset_path, stack)], stack in Spawn/Update; renderer in Sprite/Mesh/Ribbon.

    minimal 템플릿이 기본 스폰 모듈(SpawnRate 등)을 포함할 수 있어 그대로 두면
    burst 블록에서 스폰 모듈이 중복돼 'Cannot Set external constant ... SpawnBurst/SpawnRemainder'
    워닝이 난다. 추가 전에 기본 모듈 확인 → ①burst 블록이면 기본 SpawnRate 제거
    ②이미 있는 모듈은 추가 생략.
    """
    added = NS.add_emitter(PALETTE, "minimal", name)
    if not added:
        log.append(f"{name}: add_emitter FAILED")
        return

    def cur_modules():
        try:
            mods = ES.list_modules(PALETTE, name, "")
            return [str(getattr(m, "name", None) or getattr(m, "module_name", None) or m) for m in (mods or [])]
        except Exception:
            return []

    base = cur_modules()
    want_names = [p.rsplit("/", 1)[-1] for p, _ in modules]
    wants_burst = any("SpawnBurst" in w for w in want_names)
    wants_rate = any(w == "SpawnRate" for w in want_names)

    if wants_burst and not wants_rate:
        for bm in base:
            if "SpawnRate" in bm:
                ok = ES.remove_module(PALETTE, name, bm)
                log.append(f"{name}: remove default {bm} -> {'OK' if ok else 'FAIL'}")
        base = cur_modules()

    for path, stack in modules:
        short = path.rsplit("/", 1)[-1]
        if any(short.lower() == bm.lower() for bm in base):
            log.append(f"{name}: skip {short} (already in minimal)")
            continue
        ok = ES.add_module(PALETTE, name, path, stack)
        if not ok:
            log.append(f"{name}: add {short} -> FAIL")
    if renderer:
        current = ES.list_renderers(PALETTE, name)
        if not any(str(r.renderer_type).lower() == renderer.lower() for r in current):
            ES.add_renderer(PALETTE, name, renderer)
    log.append(f"{name}: OK (base was {base})")


def normalize_renderer(name, desired_type):
    """Keep exactly one renderer of the block's intended type."""
    renderers = list(ES.list_renderers(PALETTE, name) or [])
    matches = [r for r in renderers if str(r.renderer_type).lower() == desired_type.lower()]
    if not matches:
        if not ES.add_renderer(PALETTE, name, desired_type):
            log.append(f"{name}: add {desired_type} renderer FAIL")
            return False
        renderers = list(ES.list_renderers(PALETTE, name) or [])
        matches = [r for r in renderers if str(r.renderer_type).lower() == desired_type.lower()]
    if not matches:
        return False
    keep_index = int(matches[0].renderer_index)
    remove_indices = sorted(
        (int(r.renderer_index) for r in renderers if int(r.renderer_index) != keep_index),
        reverse=True,
    )
    ok = True
    for renderer_index in remove_indices:
        removed = ES.remove_renderer(PALETTE, name, renderer_index)
        ok = removed and ok
        log.append(f"{name}: remove extra renderer {renderer_index} -> {'OK' if removed else 'FAIL'}")
    final = list(ES.list_renderers(PALETTE, name) or [])
    valid = len(final) == 1 and str(final[0].renderer_type).lower() == desired_type.lower()
    log.append(f"{name}: renderer contract {desired_type} -> {'OK' if valid else 'FAIL'}")
    return ok and valid


def ensure_quality_modules(name, renderer_type):
    """Install neutral production hooks; recipes then drive their inputs/curves."""
    existing = {
        str(m.module_name).lower()
        for m in (ES.list_modules(PALETTE, name, "") or [])
    }
    wanted = []
    if renderer_type == "Sprite":
        wanted.append((M["scale_sprite"], "Update"))
    elif renderer_type == "Mesh":
        wanted.append((M["scale_mesh"], "Update"))
    elif renderer_type == "Ribbon":
        wanted.extend(((M["init_ribbon"], "Spawn"), (M["scale_ribbon"], "Update")))
    wanted.extend(((M["scale_color"], "Update"), (M["dynamic_material"], "Update")))

    ok = True
    for path, stage in wanted:
        short = path.rsplit("/", 1)[-1]
        if short.lower() in existing:
            continue
        added = ES.add_module(PALETTE, name, path, stage)
        ok = added and ok
        log.append(f"{name}: quality module {short} -> {'OK' if added else 'FAIL'}")
        if added:
            existing.add(short.lower())
    return ok

if ensure_system():
    existing = [str(e.emitter_name) for e in (NS.list_emitters(PALETTE) or [])] if hasattr(NS, "list_emitters") else []

    def need(n):
        return n not in existing

    if need("EM_BurstSparks"):
        block("EM_BurstSparks",
              [(M["burst"], "Spawn"), (M["init"], "Spawn"), (M["velpt"], "Spawn"),
               (M["grav"], "Update"), (M["drag"], "Update"), (M["solve"], "Update")],
              "Sprite")
    if need("EM_ExpandingRing"):
        block("EM_ExpandingRing",
              [(M["burst"], "Spawn"), (M["init"], "Spawn")],
              "Mesh")
    if need("EM_RisingWisps"):
        block("EM_RisingWisps",
              [(M["rate"], "Spawn"), (M["init"], "Spawn"), (M["vel"], "Spawn"),
               (M["curl"], "Update"), (M["drag"], "Update"), (M["solve"], "Update")],
              "Sprite")
    if need("EM_GlowCore"):
        block("EM_GlowCore",
              [(M["rate"], "Spawn"), (M["init"], "Spawn")],
              "Sprite")
    if need("EM_GroundDust"):
        block("EM_GroundDust",
              [(M["burst"], "Spawn"), (M["init"], "Spawn"), (M["velpt"], "Spawn"),
               (M["drag"], "Update"), (M["solve"], "Update")],
              "Sprite")
    if need("EM_ElectricArc"):
        block("EM_ElectricArc",
              [(M["burst"], "Spawn"), (M["init"], "Spawn"), (M["velpt"], "Spawn"),
               (M["drag"], "Update"), (M["solve"], "Update")],
              "Ribbon")
    if need("EM_MeshDebris"):
        block("EM_MeshDebris",
              [(M["burst"], "Spawn"), (M["init"], "Spawn"), (M["velpt"], "Spawn"),
               (M["grav"], "Update"), (M["drag"], "Update"), (M["solve"], "Update")],
              "Mesh")
    if need("EM_RibbonTrail"):
        block("EM_RibbonTrail",
              [(M["rate"], "Spawn"), (M["init"], "Spawn")],
              "Ribbon")

    contracts_ok = True
    for emitter_name, renderer_type in BLOCK_RENDERERS.items():
        if emitter_name in [str(e.emitter_name) for e in (NS.list_emitters(PALETTE) or [])]:
            contracts_ok = normalize_renderer(emitter_name, renderer_type) and contracts_ok
            contracts_ok = ensure_quality_modules(emitter_name, renderer_type) and contracts_ok

    compiled = NS.compile_with_results(PALETTE)
    compile_ok = bool(getattr(compiled, "success", False))
    log.append(
        f"palette compile: {'OK' if compile_ok else 'FAIL'} "
        f"({getattr(compiled, 'error_count', 0)} errors, {getattr(compiled, 'warning_count', 0)} warnings)"
    )
    if compile_ok and contracts_ok:
        saved = unreal.EditorAssetLibrary.save_asset(PALETTE, only_if_is_dirty=False)
        log.append(f"palette saved: {saved}")
    else:
        log.append("palette NOT SAVED: renderer/module contract or compilation failed")

print("\n".join(log))
