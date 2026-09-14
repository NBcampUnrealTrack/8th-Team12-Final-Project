# levy_vfx — 이펙트 원샷 빌더 (도구 왕복 최소화의 핵심)
#
# 왜: 이펙트 1개에 execute_python_code를 30~40번 나눠 부르면 LLM 왕복마다 수 초~수십 초가
# 쌓여 수 분~십수 분 걸린다. 이 모듈은 spec(dict) 하나를 받아 시스템 생성→블록 조립→값 설정
# →컴파일까지 일괄 처리한다. 레비는 spec 작성 + build(spec) "1회 호출"이면 끝.
#
# 사용 (execute_python_code 1회):
#   import unreal, sys; sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
#   import levy_vfx
#   print(levy_vfx.build({
#       "path": "/Game/VFX/NS_FireBurst",
#       "blocks": [
#           {"from": "EM_BurstSparks", "as": "Sparks",
#            "set": [["Spawn Burst Instantaneous", "Spawn Count", "80"],
#                    ["Initialize Particle", "Lifetime", "1.2"]],
#            "tint": "(3.0, 1.0, 0.2)"},
#           {"from": "EM_GlowCore", "as": "Core",
#            "set": [["Initialize Particle", "Lifetime", "0.5"]]},
#       ],
#   }))
#
# spec 키:
#   path        : 생성/대상 NS 경로 (필수)
#   template    : (선택) 이 NS를 duplicate해서 시작 (완성형 템플릿 복제 — 조립 생략)
#   blocks      : 팔레트 블록 목록. from=팔레트 이미터, as=새 이름,
#                 set=[[모듈, 입력, 값]...], tint="(R,G,B)", material=렌더러0 머티리얼 경로
#   params      : (선택) [[User파라미터명, 값]...] — set_parameter 일괄
#   save        : 기본 True
# 반환: 단계별 결과 문자열 (실패 라인에 FAIL 표기 — 실패만 후속 수정하면 됨)

import copy
import json
import re
import uuid

import unreal
import revvy_vfx_core as vfx_core

PALETTE = "/Revvy/VFX/_Library/NS_EmitterPalette"

# 블록별 기본 머티리얼 — spec에 "material" 없으면 자동 바인딩 (기본 머티리얼=회색 박스 방지)
DEFAULT_MATERIALS = {
    "EM_BurstSparks":   "/Revvy/VFX/Materials/MI_LevySpark",
    "EM_ExpandingRing": "/Revvy/VFX/Materials/MI_LevyGlow",
    "EM_RisingWisps":   "/Revvy/VFX/Materials/MI_LevySmoke",
    "EM_GlowCore":      "/Revvy/VFX/Materials/MI_LevyGlow",
    "EM_GroundDust":    "/Revvy/VFX/Materials/MI_LevySmoke",
    "EM_RibbonTrail":   "/Revvy/VFX/Materials/MI_LevyGlow",
    "EM_ElectricArc":   "/Revvy/VFX/Materials/MI_LevyLightning",
    "EM_MeshDebris":    "/Revvy/VFX/Materials/MI_LevyGlow",
}
DEFAULT_MESHES = {
    "EM_ExpandingRing": "/Game/VFX/Generated/Meshes/SM_RevvyVFX_Ring",
    "EM_MeshDebris": "/Game/VFX/Generated/Meshes/SM_RevvyVFX_Shard",
}
POLISHED_MESHES = {
    "broken_ring": "/Game/VFX/Generated/Meshes/SM_RevvyVFX_BrokenRing",
}
POLISHED_MATERIALS = {
    "glow": "/Revvy/VFX/Materials/MI_RevvyPolishedGlow",
    "spark": "/Revvy/VFX/Materials/MI_RevvyPolishedSpark",
    "smoke": "/Revvy/VFX/Materials/MI_RevvyPolishedSmoke",
    "dark_core": "/Revvy/VFX/Materials/MI_RevvyPolishedDarkCore",
    "ring": "/Revvy/VFX/Materials/MI_RevvyPolishedRing",
    "shard": "/Revvy/VFX/Materials/MI_RevvyPolishedShard",
    "runic": "/Revvy/VFX/Materials/MI_RevvyPolishedRunic",
    "arcane_spark": "/Revvy/VFX/Materials/MI_RevvyPolishedArcaneSpark",
    "arcane_smoke": "/Revvy/VFX/Materials/MI_RevvyPolishedArcaneSmoke",
    "gpt_impact": "/Game/VFX/Generated/Materials/MI_Revvy_GPT_ImpactBurst",
}
MODULE_SCRIPTS = {
    "burst": "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous",
    "sphere_location": "/Niagara/Modules/Spawn/Location/V2/SphereLocation",
    "cone_velocity": "/Niagara/Modules/Spawn/Velocity/AddVelocityInCone",
    "vortex_force": "/Niagara/Modules/Update/Forces/VortexForce",
    "torus_location": "/Niagara/Modules/Spawn/Location/TorusLocation.TorusLocation",
    "gravity_force": "/Niagara/Modules/Update/Forces/GravityForce.GravityForce",
    "solve_forces": "/Niagara/Modules/Solvers/SolveForcesAndVelocity",
}
DEFAULT_EFFECT_TYPES = {
    "combat": "/Game/VFX/Generated/EffectTypes/ET_RevvyCombat",
    "ambient": "/Game/VFX/Generated/EffectTypes/ET_RevvyAmbient",
    "cinematic": "/Game/VFX/Generated/EffectTypes/ET_RevvyCinematic",
}
RENDERER_TYPES = {
    "EM_BurstSparks": "Sprite",
    "EM_ExpandingRing": "Mesh",
    "EM_RisingWisps": "Sprite",
    "EM_GlowCore": "Sprite",
    "EM_GroundDust": "Sprite",
    "EM_RibbonTrail": "Ribbon",
    "EM_ElectricArc": "Ribbon",
    "EM_MeshDebris": "Mesh",
}
NS = unreal.NiagaraService
ES = unreal.NiagaraEmitterService


def ensure_palette():
    """팔레트 없으면 build_emitter_palette 실행. 실패는 실패로 보고한다."""
    if unreal.load_asset(PALETTE):
        return "palette: exists"
    import importlib
    import sys

    module = sys.modules.get("build_emitter_palette")
    if module is not None:
        # 이 세션에서 이미 빌드를 시도했는데 팔레트가 여전히 없다는 뜻이다
        # (계약 실패로 NOT SAVED, 또는 자산 삭제). 단순 import 는 캐시 히트라
        # 아무것도 안 하고 낡은 성공을 보고하니, reload 로 실제 재빌드한다.
        module = importlib.reload(module)
    else:
        import build_emitter_palette as module  # import 자체가 구축 실행
    failed = any("NOT SAVED" in line for line in getattr(module, "log", []))
    return "palette: build FAILED (renderer/compile contract — see log)" if failed else "palette: built"


def _transform(part):
    return unreal.Transform(
        location=unreal.Vector(*part["location"]),
        rotation=unreal.Rotator(*part["rotation"]),
        scale=unreal.Vector(*part["scale"]),
    )


def _append_mesh_part(service, handle, part):
    transform = _transform(part)
    primitive = part["type"]
    if primitive == "box":
        return service.append_box(
            handle,
            transform,
            unreal.Vector(*part["dimensions"]),
            *part["steps"],
        )
    if primitive == "sphere":
        return service.append_sphere(
            handle, transform, part["radius"], part["steps"], part["steps"]
        )
    if primitive == "cylinder":
        return service.append_cylinder(
            handle,
            transform,
            part["radius"],
            part["height"],
            part["radial_steps"],
            part["height_steps"],
        )
    if primitive == "torus":
        return service.append_torus(
            handle,
            transform,
            part["major_radius"],
            part["minor_radius"],
            part["major_steps"],
            part["minor_steps"],
            part.get("revolve_degrees", 360.0),
            part.get("degree_offset", 0.0),
            part.get("fill_end_caps", True),
        )
    return service.append_mesh_from_static_mesh(
        handle, part["asset_path"], transform, part["lod"]
    )


def build_procedural_mesh(recipe):
    """Build a safe GPT-authored GeometryScript recipe into a StaticMesh asset."""
    spec = vfx_core.validate_mesh_recipe(recipe)
    path = spec["path"]
    if unreal.EditorAssetLibrary.does_asset_exist(path) and not spec["replace_existing"]:
        return {"success": True, "asset_path": path, "reused": True, "operations": []}

    service = unreal.GeometryScriptService
    handles = []
    operations = []
    main = "RevvyVFX_%s" % uuid.uuid4().hex
    if not service.create_dynamic_mesh(main):
        return {"success": False, "asset_path": path, "error": "create_dynamic_mesh failed"}
    handles.append(main)
    try:
        for index, part in enumerate(spec["parts"]):
            operation = part["operation"]
            if operation == "append":
                ok = _append_mesh_part(service, main, part)
            else:
                operand = "%s_%d" % (main, index)
                if not service.create_dynamic_mesh(operand):
                    raise RuntimeError("create cutter mesh failed at part %d" % index)
                handles.append(operand)
                if not _append_mesh_part(service, operand, part):
                    raise RuntimeError("append cutter failed at part %d" % index)
                if operation == "subtract":
                    ok = service.boolean_subtract(main, operand)
                else:
                    ok = service.boolean_intersect(main, operand)
            operations.append({"part": index, "type": part["type"], "operation": operation, "success": bool(ok)})
            if not ok:
                raise RuntimeError("mesh operation failed at part %d" % index)
        result_path = service.convert_dynamic_mesh_to_static_mesh(main, path)
        if not result_path:
            raise RuntimeError("convert_dynamic_mesh_to_static_mesh failed")
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        return {"success": True, "asset_path": str(result_path), "reused": False, "operations": operations}
    except Exception as exc:
        return {"success": False, "asset_path": path, "error": str(exc), "operations": operations}
    finally:
        for handle in reversed(handles):
            service.dispose_dynamic_mesh(handle)


def ensure_standard_meshes():
    recipes = {
        "ring": {
            "path": DEFAULT_MESHES["EM_ExpandingRing"],
            "parts": [
                {
                    "type": "torus",
                    "major_radius": 50.0,
                    "minor_radius": 2.0,
                    "major_steps": 96,
                    "minor_steps": 8,
                }
            ],
        },
        "shard": {
            "path": DEFAULT_MESHES["EM_MeshDebris"],
            "parts": [
                {
                    "type": "cylinder",
                    "radius": 4.0,
                    "height": 28.0,
                    "radial_steps": 3,
                    "height_steps": 1,
                }
            ],
        },
        "broken_ring": {
            "path": POLISHED_MESHES["broken_ring"],
            "parts": [
                {
                    "type": "torus",
                    "major_radius": 50.0,
                    "minor_radius": 2.6,
                    "major_steps": 40,
                    "minor_steps": 5,
                    "revolve_degrees": 126.0,
                    "degree_offset": 12.0,
                },
                {
                    "type": "torus",
                    "major_radius": 50.0,
                    "minor_radius": 1.8,
                    "major_steps": 44,
                    "minor_steps": 5,
                    "revolve_degrees": 142.0,
                    "degree_offset": 202.0,
                },
            ],
        },
    }
    return {name: build_procedural_mesh(recipe) for name, recipe in recipes.items()}


def ensure_effect_type(profile="combat"):
    """Create a shared Effect Type with engine defaults when the project has none."""
    profile_name = str(profile or "combat").lower()
    path = DEFAULT_EFFECT_TYPES.get(profile_name)
    if not path:
        raise vfx_core.VfxSpecError(
            "effect type profile must be combat, ambient, or cinematic"
        )
    existing = unreal.load_asset(path)
    if existing:
        return {"success": True, "asset_path": path, "reused": True}
    name = path.rsplit("/", 1)[-1]
    folder = path.rsplit("/", 1)[0]
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name,
        folder,
        unreal.NiagaraEffectType,
        unreal.NiagaraEffectTypeFactoryNew(),
    )
    if not asset:
        return {"success": False, "asset_path": path, "reused": False}
    saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return {
        "success": bool(saved),
        "asset_path": path,
        "reused": False,
    }


def _ensure_system_effect_type(system_path, profile):
    system = unreal.load_asset(system_path)
    if not system:
        return False, ""
    current = system.get_editor_property("effect_type")
    if current:
        return True, current.get_path_name()
    result = ensure_effect_type(profile)
    if not result["success"]:
        return False, result["asset_path"]
    effect_type = unreal.load_asset(result["asset_path"])
    if not effect_type:
        return False, result["asset_path"]
    system.set_editor_property("effect_type", effect_type)
    return True, result["asset_path"]


def _renderer_index(path, emitter, desired_type):
    renderers = list(ES.list_renderers(path, emitter) or [])
    matches = [r for r in renderers if str(r.renderer_type).lower() == desired_type.lower()]
    if not matches:
        return -1, renderers
    return int(matches[0].renderer_index), renderers


def _asset_reference(path):
    asset = unreal.load_asset(path)
    if not asset:
        return ""
    return "%s'%s'" % (asset.get_class().get_path_name(), asset.get_path_name())


def _bind_renderer(path, emitter, source_block, material=None, mesh=None):
    desired_type = RENDERER_TYPES.get(source_block, "Sprite")
    index, renderers = _renderer_index(path, emitter, desired_type)
    if index < 0:
        return False, ["missing %s renderer" % desired_type]
    messages = []
    ok = True
    for renderer in renderers:
        renderer_index = int(renderer.renderer_index)
        enabled = renderer_index == index
        changed = ES.enable_renderer(path, emitter, renderer_index, enabled)
        ok = changed and ok
        if not enabled:
            messages.append("disabled extra %s renderer %d" % (renderer.renderer_type, renderer_index))
    if material:
        bound = ES.set_renderer_property(path, emitter, index, "Material", material)
        details = ES.get_renderer_details(path, emitter, index)
        bound = bool(getattr(details, "material_path", ""))
        if desired_type == "Mesh" and not bound:
            reference = _asset_reference(material)
            bound = bool(reference) and ES.set_renderer_property(
                path, emitter, index, "bOverrideMaterials", "true"
            ) and ES.set_renderer_property(
                path,
                emitter,
                index,
                "OverrideMaterials",
                "((ExplicitMat=%s))" % reference,
            )
            details = ES.get_renderer_details(path, emitter, index)
            bound = bound and bool(getattr(details, "material_path", ""))
        ok = bound and ok
        messages.append("material[%d]=%s: %s" % (index, material.rsplit("/", 1)[-1], "OK" if bound else "FAIL"))
    if desired_type == "Mesh":
        mesh_path = mesh or DEFAULT_MESHES.get(source_block)
        bound = bool(mesh_path) and ES.set_renderer_property(path, emitter, index, "ParticleMesh", mesh_path)
        details = ES.get_renderer_details(path, emitter, index)
        bound = bool(getattr(details, "mesh_path", ""))
        if mesh_path and not bound:
            reference = _asset_reference(mesh_path)
            bound = bool(reference) and ES.set_renderer_property(
                path,
                emitter,
                index,
                "Meshes",
                "((Mesh=%s))" % reference,
            )
            details = ES.get_renderer_details(path, emitter, index)
            bound = bound and bool(getattr(details, "mesh_path", ""))
        ok = bound and ok
        messages.append("mesh[%d]=%s: %s" % (index, (mesh_path or "missing").rsplit("/", 1)[-1], "OK" if bound else "FAIL"))
    return ok, messages


def _set_module_input(path, emitter, module, input_name, value):
    if ES.set_module_input(path, emitter, module, input_name, str(value)):
        return True
    parameters = NS.list_rapid_iteration_params(path, emitter) or []
    exact_name = vfx_core.find_rapid_iteration_parameter(
        parameters,
        emitter,
        module,
        input_name,
    )
    return bool(
        exact_name
        and NS.set_rapid_iteration_param(path, emitter, exact_name, str(value))
    )


def _normalized_module_name(value):
    return "".join(ch for ch in str(value or "").casefold() if ch.isalnum())


def _current_modules(path, emitter):
    return list(ES.list_modules(path, emitter, "") or [])


def _remove_module_if_present(path, emitter, requested_name):
    wanted = _normalized_module_name(requested_name)
    matches = [
        str(module.module_name)
        for module in _current_modules(path, emitter)
        if _normalized_module_name(module.module_name) == wanted
    ]
    if not matches:
        return True
    return all(ES.remove_module(path, emitter, name) for name in matches)


def _ensure_module(path, emitter, script_or_key, stage):
    script_path = MODULE_SCRIPTS.get(script_or_key, script_or_key)
    short_name = script_path.rsplit("/", 1)[-1].split(".", 1)[0]
    wanted = _normalized_module_name(short_name)
    if any(
        _normalized_module_name(module.module_name) == wanted
        for module in _current_modules(path, emitter)
    ):
        return True
    return bool(ES.add_module(path, emitter, script_path, stage))


def _validate_preserved_renderers(path, emitter):
    renderers = list(ES.list_renderers(path, emitter) or [])
    if not renderers:
        return False, ["template has no renderer"]
    messages = []
    ok = True
    for renderer in renderers:
        if not bool(getattr(renderer, "is_enabled", True)):
            continue
        index = int(renderer.renderer_index)
        renderer_type = str(renderer.renderer_type)
        details = ES.get_renderer_details(path, emitter, index)
        material = str(getattr(details, "material_path", "") or "") if details else ""
        mesh = str(getattr(details, "mesh_path", "") or "") if details else ""
        if renderer_type.casefold() not in ("light", "component") and not material:
            ok = False
            messages.append("preserved %s renderer %d missing material" % (renderer_type, index))
        if renderer_type.casefold() == "mesh" and not mesh:
            ok = False
            messages.append("preserved mesh renderer %d missing mesh" % index)
    if ok:
        messages.append("preserved template renderers: OK")
    return ok, messages


def _set_renderer_properties(path, emitter, properties, renderer_type=""):
    renderers = list(ES.list_renderers(path, emitter) or [])
    candidates = [
        renderer
        for renderer in renderers
        if not renderer_type
        or str(renderer.renderer_type).casefold() == str(renderer_type).casefold()
    ]
    if not candidates:
        return False
    index = int(candidates[0].renderer_index)
    return all(
        ES.set_renderer_property(path, emitter, index, str(name), str(value))
        for name, value in (properties or {}).items()
    )


def _set_system_fixed_bounds(path, bounds):
    system = unreal.load_asset(path)
    if not system or not isinstance(bounds, (list, tuple)) or len(bounds) != 2:
        return False
    minimum = tuple(float(value) for value in bounds[0])
    maximum = tuple(float(value) for value in bounds[1])
    if len(minimum) != 3 or len(maximum) != 3:
        return False
    fixed_bounds = unreal.Box()
    fixed_bounds.set_editor_properties(
        {
            "min": unreal.Vector(*minimum),
            "max": unreal.Vector(*maximum),
            "is_valid": True,
        }
    )
    system.modify()
    system.set_editor_property("fixed_bounds", fixed_bounds)
    value = system.get_editor_property("fixed_bounds")
    return bool(
        value.is_valid
        and tuple(float(getattr(value.min, axis)) for axis in ("x", "y", "z")) == minimum
        and tuple(float(getattr(value.max, axis)) for axis in ("x", "y", "z")) == maximum
    )


def capture_runtime_evidence(path, sample_times=(0.05, 0.2, 0.6), tick_delta=1.0 / 60.0):
    """Advance transient components and inspect real particle attributes via Sim Cache."""
    system = unreal.load_asset(path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    report = {"success": True, "sample_times": list(sample_times), "emitters": {}, "errors": []}
    if not system or not world:
        report["success"] = False
        report["errors"].append("system or editor world unavailable")
        return report

    for sample_time in sample_times:
        component = None
        try:
            component = unreal.NiagaraFunctionLibrary.spawn_system_at_location(
                world,
                system,
                unreal.Vector(0, 0, 0),
                unreal.Rotator(0, 0, 0),
                unreal.Vector(1, 1, 1),
                False,
                True,
                unreal.NCPoolMethod.NONE,
                False,
            )
            if not component:
                raise RuntimeError("spawn_system_at_location returned None")
            ticks = max(1, int(round(float(sample_time) / float(tick_delta))))
            component.advance_simulation(ticks, float(tick_delta))
            cache = unreal.new_object(unreal.NiagaraSimCache)
            parameters = unreal.NiagaraSimCacheCreateParameters(include_debug_data=True)
            cache = unreal.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(
                cache,
                parameters,
                component,
                False,
                float(tick_delta),
            )
            if not cache or cache.get_num_frames() < 1:
                raise RuntimeError("Sim Cache capture returned no frame")
            for emitter_name_value in cache.get_emitter_names():
                emitter_name = str(emitter_name_value)
                positions = cache.read_position_attribute("Position", emitter_name_value, True, 0)
                sprite_sizes = cache.read_vector2_attribute("SpriteSize", emitter_name_value, 0)
                mesh_scales = cache.read_vector_attribute("Scale", emitter_name_value, 0)
                ribbon_widths = cache.read_float_attribute("RibbonWidth", emitter_name_value, 0)
                colors = cache.read_color_attribute("Color", emitter_name_value, 0)
                evidence = report["emitters"].setdefault(
                    emitter_name,
                    {
                        "max_particles": 0,
                        "max_position_abs": [0.0, 0.0, 0.0],
                        "max_sprite_extent": 0.0,
                        "max_mesh_scale": 0.0,
                        "max_ribbon_width": 0.0,
                        "max_alpha": 0.0,
                        "samples": [],
                    },
                )
                count = len(positions)
                position_abs = [
                    max((abs(float(getattr(value, axis))) for value in positions), default=0.0)
                    for axis in ("x", "y", "z")
                ]
                sprite_extent = max(
                    (max(abs(float(value.x)), abs(float(value.y))) for value in sprite_sizes),
                    default=0.0,
                )
                mesh_scale = max(
                    (
                        max(abs(float(value.x)), abs(float(value.y)), abs(float(value.z)))
                        for value in mesh_scales
                    ),
                    default=0.0,
                )
                ribbon_width = max((abs(float(value)) for value in ribbon_widths), default=0.0)
                alpha = max((abs(float(value.a)) for value in colors), default=0.0)
                evidence["max_particles"] = max(evidence["max_particles"], count)
                evidence["max_position_abs"] = [
                    max(previous, current)
                    for previous, current in zip(evidence["max_position_abs"], position_abs)
                ]
                evidence["max_sprite_extent"] = max(evidence["max_sprite_extent"], sprite_extent)
                evidence["max_mesh_scale"] = max(evidence["max_mesh_scale"], mesh_scale)
                evidence["max_ribbon_width"] = max(evidence["max_ribbon_width"], ribbon_width)
                evidence["max_alpha"] = max(evidence["max_alpha"], alpha)
                evidence["samples"].append(
                    {
                        "time": float(sample_time),
                        "particles": count,
                        "position_abs": position_abs,
                        "sprite_extent": sprite_extent,
                        "mesh_scale": mesh_scale,
                        "ribbon_width": ribbon_width,
                    }
                )
        except Exception as exc:
            report["success"] = False
            report["errors"].append("%.3fs: %s" % (sample_time, exc))
        finally:
            if component:
                component.deactivate()
    return report


def build(spec):
    out = []
    failures = []
    path = vfx_core.validate_asset_path(spec["path"], "NS_")
    blocks = list(spec.get("blocks", []))

    def record(label, ok):
        out.append(f"{label}: {'OK' if ok else 'FAIL'}")
        if not ok:
            failures.append(label)
        return bool(ok)

    if blocks:
        out.append(ensure_palette())
    needs_standard_mesh = any(
        block.get("from") in DEFAULT_MESHES and not block.get("mesh")
        for block in blocks
    ) or any(
        block.get("mesh") in set(POLISHED_MESHES.values())
        for block in blocks
    )
    if needs_standard_mesh:
        mesh_results = ensure_standard_meshes()
        for name, result in mesh_results.items():
            record("standard mesh %s" % name, result.get("success", False))

    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        template = spec.get("template")
        if template:
            record(f"duplicate {template} -> {path}", unreal.AssetDiscoveryService.duplicate_asset(template, path))
        else:
            name = path.rsplit("/", 1)[-1]
            folder = path.rsplit("/", 1)[0]
            result = NS.create_system(name, folder)
            record(f"create_system {path}", bool(getattr(result, "success", False)))
    else:
        out.append(f"system exists: {path}")

    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        failures.append("system unavailable")
        return "\n".join(out + ["RESULT: FAIL (system unavailable)"])

    properties_from = spec.get("properties_from")
    if properties_from:
        record(
            f"copy system properties from {properties_from}",
            NS.copy_system_properties(path, properties_from),
        )

    if spec.get("quality_gate") or spec.get("effect_type_profile"):
        effect_ok, effect_path = _ensure_system_effect_type(
            path,
            spec.get("effect_type_profile", spec.get("profile", "combat")),
        )
        record("effect type %s" % (effect_path or "unavailable"), effect_ok)

    if spec.get("fixed_bounds"):
        record(
            "fixed bounds %s" % (spec["fixed_bounds"],),
            _set_system_fixed_bounds(path, spec["fixed_bounds"]),
        )

    for block in blocks:
        source = block.get("from") or block.get("fallback_from") or block.get("source_emitter")
        if not source:
            failures.append("block source missing")
            continue
        emitter = block.get("as", source)
        if NS.emitter_exists(path, emitter):
            out.append(f"reuse emitter: {emitter}")
        else:
            copied_name = ""
            emitter_asset = str(block.get("asset", "") or "")
            source_system = str(block.get("source_system", "") or "")
            source_emitter = str(block.get("source_emitter", "") or "")
            if emitter_asset and unreal.load_asset(emitter_asset):
                copied_name = NS.add_emitter(path, emitter_asset, emitter)
                label = f"add template {emitter_asset} -> {emitter}"
            elif (
                source_system
                and source_emitter
                and unreal.load_asset(source_system)
                and NS.emitter_exists(source_system, source_emitter)
            ):
                copied_name = NS.copy_emitter(source_system, source_emitter, path, emitter)
                label = f"copy template {source_system}:{source_emitter} -> {emitter}"
            else:
                copied_name = NS.copy_emitter(PALETTE, source, path, emitter)
                label = f"copy fallback {source} -> {emitter}"
            if not record(label, bool(copied_name)):
                continue
            emitter = str(copied_name)

        for module_name in block.get("remove_modules", []):
            record(
                f"  remove {emitter}.{module_name}",
                _remove_module_if_present(path, emitter, module_name),
            )
        for module_spec in block.get("add_modules", []):
            module_path, stage = module_spec
            record(
                f"  add {emitter}.{module_path}",
                _ensure_module(path, emitter, module_path, stage),
            )

        for module, input_name, value in block.get("set", []):
            record(
                f"  set {emitter}.{module}.{input_name}={value}",
                _set_module_input(path, emitter, module, input_name, value),
            )
        if str(spec.get("lifecycle", "")).casefold() == "one_shot":
            record(
                f"  lifecycle {emitter}=Self/Once",
                ES.set_emitter_lifecycle(path, emitter, "Self", "Once"),
            )
        if block.get("tint"):
            record(
                f"  tint {emitter}={block['tint']}",
                ES.set_color_tint(path, emitter, block["tint"]),
            )

        if block.get("preserve_renderer"):
            renderer_ok, messages = _validate_preserved_renderers(path, emitter)
        else:
            material = block.get("material") or DEFAULT_MATERIALS.get(source)
            renderer_ok, messages = _bind_renderer(
                path,
                emitter,
                source,
                material=material,
                mesh=block.get("mesh"),
            )
        for message in messages:
            out.append("  " + message)
        if not renderer_ok:
            failures.append("renderer contract %s" % emitter)
        renderer_properties = block.get("renderer_properties") or {}
        if renderer_properties:
            record(
                f"  renderer properties {emitter}",
                _set_renderer_properties(
                    path,
                    emitter,
                    renderer_properties,
                    renderer_type=block.get("renderer_type", ""),
                ),
            )

    for parameter_name, parameter_value in spec.get("params", []):
        record(
            f"param {parameter_name}={parameter_value}",
            NS.set_parameter(path, parameter_name, str(parameter_value)),
        )

    preview_range = spec.get("preview_range")
    if preview_range:
        preview_start, preview_end = map(float, preview_range)
        record(
            "preview range %.2f-%.2fs" % (preview_start, preview_end),
            NS.set_editor_playback_range(path, preview_start, preview_end),
        )

    compiled = NS.compile_with_results(path)
    compile_ok = bool(getattr(compiled, "success", False))
    out.append(
        "compile: %s (%s errors, %s warnings)"
        % (
            "OK" if compile_ok else "FAIL",
            getattr(compiled, "error_count", 0),
            getattr(compiled, "warning_count", 0),
        )
    )
    for message in list(getattr(compiled, "errors", []) or []):
        out.append("  ERROR: " + str(message))
    for message in list(getattr(compiled, "warnings", []) or []):
        out.append("  WARNING: " + str(message))
    if not compile_ok:
        failures.append("compile")
    if spec.get("warnings_as_errors") and getattr(compiled, "warning_count", 0):
        failures.append("compile warnings")

    quality = None
    if spec.get("quality_gate"):
        quality = audit_system(
            path,
            phases=spec.get("phases", []),
            profile=spec.get("profile", "combat"),
            lifecycle=spec.get("lifecycle", ""),
            threshold=int(spec.get("quality_threshold", 78)),
            compile_result=compiled,
            runtime_samples=spec.get("simulation_samples", (0.05, 0.2, 0.6)),
        )
        out.append("quality: %s/100 (%s)" % (quality["score"], "PASS" if quality["passed"] else "FAIL"))
        for issue in quality["issues"]:
            out.append("  %s %s: %s" % (issue["severity"].upper(), issue["code"], issue["message"]))
        if not quality["passed"]:
            failures.append("quality gate")

    strict = bool(spec.get("strict", True))
    passed = compile_ok and (not failures or not strict)
    if spec.get("save", True) and passed:
        record("save", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
        passed = not failures or not strict
    elif spec.get("save", True):
        out.append("save: SKIPPED (fix failures first)")
    out.append("RESULT: %s" % ("PASS" if passed else "FAIL"))
    return "\n".join(out)


def _material_temporal_contract(material_path):
    """Return whether a renderer material has a particle-time driven master."""
    if not material_path:
        return False, ""

    try:
        material = unreal.load_asset(material_path)
    except Exception:
        return False, ""
    if not material:
        return False, ""

    base_material = material
    material_instance_class = getattr(unreal, "MaterialInstance", None)
    visited = set()
    try:
        while material_instance_class and isinstance(base_material, material_instance_class):
            current_path = str(base_material.get_path_name())
            if current_path in visited:
                return False, current_path
            visited.add(current_path)
            parent = base_material.get_editor_property("parent")
            if not parent:
                break
            base_material = parent
    except Exception:
        return False, ""

    try:
        master_path = str(base_material.get_path_name())
    except Exception:
        master_path = ""
    try:
        expressions = unreal.MaterialEditingLibrary.get_material_expressions(base_material) or []
        temporal_classes = {
            "materialexpressionparticlerelativetime",
            "materialexpressiondynamicparameter",
        }
        for expression in expressions:
            class_name = str(expression.get_class().get_name()).lower()
            if class_name in temporal_classes:
                return True, master_path
    except Exception:
        pass
    return False, master_path


def audit_system(
    path,
    phases=None,
    profile="combat",
    lifecycle="",
    threshold=78,
    compile_result=None,
    runtime_samples=None,
    runtime_evidence=None,
):
    """Run a structural production-quality gate on a live Niagara system."""
    path = vfx_core.validate_asset_path(path, "NS_")
    compiled = compile_result or NS.compile_with_results(path)
    properties = NS.get_system_properties(path)
    observation = {
        "compile": {
            "success": bool(getattr(compiled, "success", False)),
            "error_count": int(getattr(compiled, "error_count", 0)),
            "warning_count": int(getattr(compiled, "warning_count", 0)),
            "errors": [str(value) for value in (getattr(compiled, "errors", []) or [])],
            "warnings": [str(value) for value in (getattr(compiled, "warnings", []) or [])],
        },
        "phases": list(phases or []),
        "system": {
            "effect_type": str(getattr(properties, "effect_type_path", "") or "") if properties else "",
            "fixed_bounds": bool(getattr(properties, "fixed_bounds", False)) if properties else False,
            "fixed_bounds_value": str(getattr(properties, "fixed_bounds_value", "") or "") if properties else "",
            "deterministic": bool(getattr(properties, "determinism", False)) if properties else False,
            "lifecycle": str(lifecycle or ""),
        },
        "emitters": [],
    }
    for emitter_info in NS.list_emitters(path) or []:
        if not bool(getattr(emitter_info, "is_enabled", True)):
            continue
        emitter_name = str(emitter_info.emitter_name)
        module_infos = [
            module
            for module in (ES.list_modules(path, emitter_name, "") or [])
            if bool(getattr(module, "is_enabled", True))
        ]
        module_inputs = {}
        loop_delay = 0.0
        burst_times = []
        spawn_count = 0
        continuous_spawn = False
        lifecycle_info = NS.get_emitter_lifecycle(path, emitter_name)

        def numeric(value, default=0.0):
            match = re.search(r"[-+]?(?:\d+\.?\d*|\.\d+)", str(value or ""))
            return float(match.group(0)) if match else float(default)

        for module in module_infos:
            module_name = str(module.module_name)
            inputs = {
                str(item.input_name): str(item.current_value)
                for item in (getattr(module, "inputs", []) or [])
            }
            module_inputs[module_name] = inputs
            normalized_module = _normalized_module_name(module_name)
            normalized_inputs = {
                _normalized_module_name(name): value for name, value in inputs.items()
            }
            if normalized_module == "emitterstate":
                loop_delay = numeric(normalized_inputs.get("loopdelay"), 0.0)
            elif normalized_module.startswith("spawnburst"):
                burst_times.append(numeric(normalized_inputs.get("spawntime"), 0.0))
                spawn_count = max(
                    spawn_count,
                    int(round(numeric(normalized_inputs.get("spawncount"), 0.0))),
                )
            elif normalized_module == "spawnrate":
                continuous_spawn = numeric(normalized_inputs.get("spawnrate"), 0.0) > 0.0
        emitter = {
            "name": emitter_name,
            "sim_target": str(getattr(emitter_info, "sim_target", "")),
            "modules": [str(module.module_name) for module in module_infos],
            "module_inputs": module_inputs,
            "spawn_time": loop_delay + min(burst_times or [0.0]),
            "loop_delay": loop_delay,
            "spawn_count": spawn_count,
            "continuous_spawn": continuous_spawn,
            "lifecycle_resolved": bool(getattr(lifecycle_info, "resolved", False)) if lifecycle_info else False,
            "life_cycle_mode": str(getattr(lifecycle_info, "life_cycle_mode", "") or "") if lifecycle_info else "",
            "loop_behavior": str(getattr(lifecycle_info, "loop_behavior", "") or "") if lifecycle_info else "",
            "inactive_response": str(getattr(lifecycle_info, "inactive_response", "") or "") if lifecycle_info else "",
            "renderers": [],
        }
        for renderer in ES.list_renderers(path, emitter_name) or []:
            details = ES.get_renderer_details(path, emitter_name, int(renderer.renderer_index))
            material_path = str(getattr(details, "material_path", "") or "") if details else ""
            temporal_material, material_master = _material_temporal_contract(material_path)
            emitter["renderers"].append(
                {
                    "type": str(renderer.renderer_type),
                    "index": int(renderer.renderer_index),
                    "enabled": bool(getattr(renderer, "is_enabled", True)),
                    "material": material_path,
                    "material_master": material_master,
                    "temporal_material": temporal_material,
                    "mesh": str(getattr(details, "mesh_path", "") or "") if details else "",
                    "alignment": str(getattr(details, "alignment", "") or "") if details else "",
                    "facing_mode": str(getattr(details, "facing_mode", "") or "") if details else "",
                    "sub_image_size": str(getattr(details, "sub_image_size", "") or "") if details else "",
                }
            )
        observation["emitters"].append(emitter)
    if runtime_evidence is not None:
        observation["runtime_simulation"] = runtime_evidence
    elif runtime_samples is not None:
        runtime_simulation = capture_runtime_evidence(
            path,
            sample_times=runtime_samples,
        )
        if not runtime_simulation.get("success", False):
            first_errors = list(runtime_simulation.get("errors", []))
            runtime_simulation = capture_runtime_evidence(
                path,
                sample_times=runtime_samples,
            )
            runtime_simulation["retry_count"] = 1
            if not runtime_simulation.get("success", False):
                runtime_simulation["errors"] = first_errors + list(
                    runtime_simulation.get("errors", [])
                )
        observation["runtime_simulation"] = runtime_simulation
    report = vfx_core.assess_niagara_observation(
        observation,
        profile=profile,
        threshold=int(threshold),
    )
    report["system_path"] = path
    report["observation"] = observation
    return report


def audit_system_json(
    path,
    phases=None,
    profile="combat",
    lifecycle="",
    threshold=78,
    runtime_samples=(0.05, 0.2, 0.6),
):
    return json.dumps(
        audit_system(
            path,
            phases=phases,
            profile=profile,
            lifecycle=lifecycle,
            threshold=threshold,
            runtime_samples=runtime_samples,
        ),
        ensure_ascii=False,
    )

# ── 프리셋: 학습 통합 산출물 — 한 줄 빌드 ───────────────────────────────
# 사용: levy_vfx.build_preset("impact_hit", "/Game/VFX/NS_HitA")
#       levy_vfx.build_preset("fire", "/Game/VFX/NS_Camp", tint="(2.0,0.6,0.1)")
# 값 출처: RECIPES/OBLIVIO_EFFECTS 수치 + CGHow 히트 스택 + Revvy 레이어링 공식.
# 모듈명은 실제 함수명(공백 없음) — "Initialize Particle"처럼 공백 넣으면 매칭 실패.
MI = "/Revvy/VFX/Materials"

PRESETS = {
    # 불 — RisingWisps(스크롤 위스프) + GlowCore. 호러 톤이면 tint 낮추기.
    "fire": {"blocks": [
        {"from": "EM_RisingWisps", "as": "Flames",
         "set": [["SpawnRate", "SpawnRate", "70"], ["InitializeParticle", "Lifetime", "0.9"]],
         "tint": "(3.0, 1.0, 0.15)", "material": MI + "/MI_LevyFireWisp"},
        {"from": "EM_GlowCore", "as": "Core",
         "set": [["InitializeParticle", "Lifetime", "0.6"]],
         "tint": "(4.0, 1.6, 0.3)", "material": MI + "/MI_LevyGlow"},
    ]},
    # 타격 임팩트 — CGHow 스택: 스파크 burst + 코어 플래시(짧게) + 먼지. FEEL heavy-hit 페어링 권장.
    "impact_hit": {"blocks": [
        {"from": "EM_BurstSparks", "as": "Sparks",
         "set": [["SpawnBurst_Instantaneous", "SpawnCount", "60"], ["InitializeParticle", "Lifetime", "0.7"]],
         "tint": "(3.0, 2.2, 0.8)", "material": MI + "/MI_LevySpark"},
        {"from": "EM_GlowCore", "as": "Flash",
         "set": [["InitializeParticle", "Lifetime", "0.15"], ["SpawnRate", "SpawnRate", "20"]],
         "tint": "(5.0, 4.0, 2.5)", "material": MI + "/MI_LevyGlow"},
        {"from": "EM_GroundDust", "as": "Dust",
         "set": [["SpawnBurst_Instantaneous", "SpawnCount", "25"], ["InitializeParticle", "Lifetime", "0.8"]],
         "tint": "(0.35, 0.3, 0.25)", "material": MI + "/MI_LevySmoke"},
    ]},
    # 연기 — translucent 단일층, 느린 상승.
    "smoke": {"blocks": [
        {"from": "EM_RisingWisps", "as": "Smoke",
         "set": [["SpawnRate", "SpawnRate", "45"], ["InitializeParticle", "Lifetime", "3.0"]],
         "tint": "(0.4, 0.4, 0.42)", "material": MI + "/MI_LevySmoke"},
    ]},
    # 음파/충격파 — OBLIVIO 레시피 압축: 확장 링(메시) + 먼지 쇼크프론트 + 차지 글로우.
    "sonic_wave": {"blocks": [
        {"from": "EM_ExpandingRing", "as": "Ring",
         "set": [["InitializeParticle", "Lifetime", "0.8"]],
         "tint": "(0.8, 1.3, 1.8)"},
        {"from": "EM_GroundDust", "as": "ShockDust",
         "set": [["SpawnBurst_Instantaneous", "SpawnCount", "40"], ["InitializeParticle", "Lifetime", "0.6"]],
         "tint": "(0.4, 0.35, 0.3)", "material": MI + "/MI_LevySmoke"},
        {"from": "EM_GlowCore", "as": "Charge",
         "set": [["InitializeParticle", "Lifetime", "0.3"]],
         "tint": "(1.5, 2.2, 3.0)", "material": MI + "/MI_LevyGlow"},
    ]},
    # 다크 오라 — 호러 절제: 가산 금지 톤(어두운 틴트), 느린 위스프 + 희미한 코어.
    "dark_aura": {"blocks": [
        {"from": "EM_RisingWisps", "as": "DarkWisps",
         "set": [["SpawnRate", "SpawnRate", "25"], ["InitializeParticle", "Lifetime", "3.5"]],
         "tint": "(0.12, 0.06, 0.18)", "material": MI + "/MI_LevySmoke"},
        {"from": "EM_GlowCore", "as": "SickRim",
         "set": [["InitializeParticle", "Lifetime", "2.0"]],
         "tint": "(0.35, 0.1, 0.4)", "material": MI + "/MI_LevyGlow"},
    ]},
    # 슬래시/트레일 — 리본 + 스파크 소량.
    "slash_trail": {"blocks": [
        {"from": "EM_RibbonTrail", "as": "Trail",
         "set": [["SpawnRate", "SpawnRate", "60"], ["InitializeParticle", "Lifetime", "0.5"]],
         "tint": "(2.5, 0.6, 0.5)"},
        {"from": "EM_BurstSparks", "as": "Bits",
         "set": [["SpawnBurst_Instantaneous", "SpawnCount", "15"], ["InitializeParticle", "Lifetime", "0.4"]],
         "tint": "(3.0, 1.0, 0.8)", "material": MI + "/MI_LevySpark"},
    ]},
}

PRODUCTION_PRESETS = {
    "combat_impact": {
        "profile": "combat",
        "lifecycle": "one_shot",
        "quality_gate": True,
        "quality_threshold": 78,
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "blocks": [
            {"from": "EM_GlowCore", "as": "Anticipation", "set": [
                ["SpawnRate", "SpawnRate", "8"],
                ["InitializeParticle", "Lifetime", "0.22"],
                ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                ["InitializeParticle", "Sprite Size", "(36,36)"],
            ],
             "tint": "(1.4, 1.8, 3.0)", "material": MI + "/MI_LevyGlow"},
            {"from": "EM_GlowCore", "as": "ImpactFlash", "set": [
                ["SpawnRate", "SpawnRate", "12"],
                ["InitializeParticle", "Lifetime", "0.12"],
                ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                ["InitializeParticle", "Sprite Size", "(64,64)"],
            ],
             "tint": "(6.0, 4.0, 2.0)", "material": MI + "/MI_LevyGlow"},
            {"from": "EM_ExpandingRing", "as": "ShockRing", "set": [
                ["InitializeParticle", "Lifetime", "0.45"],
                ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                ["InitializeParticle", "Mesh Scale", "(1,1,1)"],
                ["ScaleMeshSize", "Scale Factor", "(1.8,1.8,0.35)"],
            ],
             "tint": "(5.0, 1.5, 0.25)", "material": MI + "/MI_LevyGlow"},
            {"from": "EM_BurstSparks", "as": "PrimarySparks",
             "set": [
                 ["SpawnBurst_Instantaneous", "SpawnCount", "72"],
                 ["InitializeParticle", "Lifetime", "0.65"],
                 ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                 ["InitializeParticle", "Sprite Size", "(3,18)"],
                 ["AddVelocityFromPoint", "Velocity Strength", "700"],
             ],
             "tint": "(4.0, 2.2, 0.7)", "material": MI + "/MI_LevySpark"},
            {"from": "EM_MeshDebris", "as": "Debris",
             "set": [
                 ["SpawnBurst_Instantaneous", "SpawnCount", "14"],
                 ["InitializeParticle", "Lifetime", "0.9"],
                 ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                 ["InitializeParticle", "Mesh Scale", "(0.6,0.6,0.6)"],
                 ["AddVelocityFromPoint", "Velocity Strength", "350"],
             ],
             "tint": "(1.2, 0.7, 0.35)", "material": MI + "/MI_LevySpark"},
            {"from": "EM_GroundDust", "as": "DecayDust",
             "set": [
                 ["SpawnBurst_Instantaneous", "SpawnCount", "32"],
                 ["InitializeParticle", "Lifetime", "1.1"],
                 ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                 ["InitializeParticle", "Sprite Size", "(72,72)"],
                 ["AddVelocityFromPoint", "Velocity Strength", "250"],
             ],
             "tint": "(1.0, 0.55, 0.25)", "material": MI + "/MI_LevySmoke"},
        ],
    },
    "arcane_burst": {
        "profile": "combat",
        "lifecycle": "one_shot",
        "quality_gate": True,
        "quality_threshold": 78,
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "blocks": [
            {"from": "EM_GlowCore", "as": "ChargeCore", "set": [
                ["SpawnRate", "SpawnRate", "10"],
                ["InitializeParticle", "Lifetime", "0.35"],
                ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                ["InitializeParticle", "Sprite Size", "(40,40)"],
            ],
             "tint": "(0.8, 1.8, 4.5)", "material": MI + "/MI_LevyRunic"},
            {"from": "EM_ExpandingRing", "as": "RunicRing", "set": [
                ["InitializeParticle", "Lifetime", "0.8"],
                ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                ["InitializeParticle", "Mesh Scale", "(1,1,1)"],
                ["ScaleMeshSize", "Scale Factor", "(1.5,1.5,0.3)"],
            ],
             "tint": "(0.6, 1.4, 3.5)", "material": MI + "/MI_LevyRunic"},
            {"from": "EM_ElectricArc", "as": "ArcRibbons",
             "set": [
                 ["SpawnBurst_Instantaneous", "SpawnCount", "12"],
                 ["InitializeParticle", "Lifetime", "0.55"],
                 ["InitializeRibbon", "Lifetime", "0.55"],
                 ["InitializeRibbon", "Ribbon Width", "8"],
                 ["AddVelocityFromPoint", "Velocity Strength", "400"],
             ],
             "tint": "(1.2, 2.5, 5.0)", "material": MI + "/MI_LevyLightning"},
            {"from": "EM_BurstSparks", "as": "ArcSparks",
             "set": [
                 ["SpawnBurst_Instantaneous", "SpawnCount", "48"],
                 ["InitializeParticle", "Lifetime", "0.7"],
                 ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                 ["InitializeParticle", "Sprite Size", "(4,20)"],
                 ["AddVelocityFromPoint", "Velocity Strength", "600"],
             ],
             "tint": "(1.2, 2.0, 4.0)", "material": MI + "/MI_LevySpark"},
            {"from": "EM_RisingWisps", "as": "ResidualWisps",
             "set": [
                 ["SpawnRate", "SpawnRate", "28"],
                 ["InitializeParticle", "Lifetime", "1.4"],
                 ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                 ["InitializeParticle", "Sprite Size", "(28,28)"],
             ],
             "tint": "(0.3, 0.8, 2.0)", "material": MI + "/MI_LevySmoke"},
        ],
    },
    "combat_impact_polished": {
        "profile": "combat",
        "lifecycle": "one_shot",
        "quality_gate": True,
        "quality_threshold": 84,
        "preview_range": (0.0, 1.4),
        "fixed_bounds": ((-900, -900, -900), (900, 900, 900)),
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "simulation_samples": (0.05, 0.15, 0.35, 0.75, 1.2),
        "style_brief": {
            "primary": "directional wedge",
            "secondary": "broken arc and sparse velocity-aligned streaks",
            "palette": ["ivory", "amber", "dark crimson"],
            "timing": "anticipation, impulse, follow-through, dissipation",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "AnticipationCore",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime", "0.18"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(24,24)"],
                ],
                "tint": "(0.9,0.35,0.08)",
                "material": POLISHED_MATERIALS["glow"],
            },
            {
                "from": "EM_GlowCore",
                "as": "DarkCore",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.045"],
                    ["InitializeParticle", "Lifetime", "0.2"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(42,42)"],
                ],
                "tint": "(1,1,1)",
                "material": POLISHED_MATERIALS["dark_core"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurst",
                "fallback_from": "EM_MeshDebris",
                "as": "PrimaryImpact",
                "set": [
                    ["EmitterState", "Loop Delay", "0.05"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "7"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime Min", "0.24"],
                    ["InitializeParticle", "Lifetime Max", "0.36"],
                    ["InitializeParticle", "Mesh Uniform Scale Min", "0.55"],
                    ["InitializeParticle", "Mesh Uniform Scale Max", "1.35"],
                    ["ShapeLocation", "Cylinder Radius", "10"],
                    ["ShapeLocation", "Cylinder Height", "5"],
                ],
                "tint": "(2.2,0.55,0.08)",
                "material": POLISHED_MATERIALS["shard"],
                "mesh": "/Game/SoStylized/Demo/Meshes/SM_Sphere",
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricShockwave",
                "fallback_from": "EM_MeshDebris",
                "as": "SecondaryImpact",
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["EmitterState", "Loop Delay", "0.075"],
                    ["InitializeParticle", "Lifetime", "0.42"],
                    ["InitializeParticle", "Lifetime Min", "0.4"],
                    ["InitializeParticle", "Lifetime Max", "0.44"],
                    ["InitializeParticle", "Mesh Uniform Scale", "2.2"],
                    ["InitializeParticle", "Color", "(1.05,0.16,0.025,1)"],
                    ["ShapeLocation", "Sphere Radius", "0"],
                ],
                "material": POLISHED_MATERIALS["ring"],
                "mesh": "/Game/SoStylized/Demo/Meshes/SM_Sphere",
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "DirectionalSparks",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.06"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "15"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime Min", "0.5"],
                    ["InitializeParticle", "Lifetime Max", "0.8"],
                    ["ShapeLocation", "Cylinder Radius", "8"],
                    ["ShapeLocation", "Cylinder Height", "4"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "55"],
                    ["RandomRangeFloat001", "Minimum", "420"],
                    ["RandomRangeFloat001", "Maximum", "900"],
                    ["RandomRangeLinearColor", "Minimum", "(1.0,0.12,0.01,1)"],
                    ["RandomRangeLinearColor", "Maximum", "(1.0,0.65,0.08,1)"],
                    ["Drag", "Drag", "1.2"],
                ],
                "material": POLISHED_MATERIALS["spark"],
                "renderer_type": "Sprite",
                "renderer_properties": {"Alignment": "VelocityAligned"},
            },
            {
                "from": "EM_ExpandingRing",
                "as": "BrokenShockArc",
                "mesh": POLISHED_MESHES["broken_ring"],
                "set": [
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.09"],
                    ["InitializeParticle", "Lifetime", "0.52"],
                    ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                    ["InitializeParticle", "Mesh Scale", "(0.55,0.55,0.16)"],
                    ["ScaleMeshSize", "Scale Factor", "(1,1,1)"],
                ],
                "tint": "(1.7,0.42,0.05)",
                "material": POLISHED_MATERIALS["ring"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_DebrisBurst",
                "fallback_from": "EM_MeshDebris",
                "as": "DebrisFollowThrough",
                "set": [
                    ["EmitterState", "Loop Delay", "0.11"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime Min", "0.55"],
                    ["InitializeParticle", "Lifetime Max", "0.82"],
                    ["Drag", "Drag", "1.2"],
                ],
                "tint": "(0.72,0.19,0.035)",
                "material": POLISHED_MATERIALS["shard"],
                "mesh": "/Game/SoStylized/Effects/Meshes/SM_RockDebris01",
            },
            {
                "from": "EM_GroundDust",
                "as": "DissipationDust",
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "10"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.14"],
                    ["InitializeParticle", "Lifetime", "1.0"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(54,54)"],
                    ["AddVelocityFromPoint", "Velocity Strength", "130"],
                    ["Drag", "Drag", "2.4"],
                ],
                "tint": "(0.3,0.2,0.14)",
                "material": POLISHED_MATERIALS["smoke"],
            },
        ],
    },
    "arcane_burst_polished": {
        "profile": "combat",
        "lifecycle": "one_shot",
        "quality_gate": True,
        "quality_threshold": 84,
        "preview_range": (0.0, 1.6),
        "fixed_bounds": ((-850, -850, -850), (850, 850, 850)),
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "simulation_samples": (0.05, 0.25, 0.55, 0.95, 1.4),
        "style_brief": {
            "primary": "curved electric ribbon",
            "secondary": "rune circle and counter-rotating shockwave",
            "palette": ["ice blue", "violet", "deep navy"],
            "timing": "charge, implosion, electric release, residual curl",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "RunicCharge",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime", "0.36"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(72,72)"],
                ],
                "tint": "(0.15,0.65,1.8)",
                "material": POLISHED_MATERIALS["runic"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurst",
                "fallback_from": "EM_GlowCore",
                "as": "PrimaryElectricBurst",
                "set": [["EmitterState", "Loop Delay", "0.18"]],
                "preserve_renderer": True,
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricShockwave",
                "fallback_from": "EM_ExpandingRing",
                "as": "ElectricShockwave",
                "set": [
                    ["EmitterState", "Loop Delay", "0.21"],
                    ["InitializeParticle", "Lifetime Min", "0.5"],
                    ["InitializeParticle", "Lifetime Max", "1.0"],
                ],
                "preserve_renderer": True,
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "ElectricParticles",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.22"],
                    ["SpawnBurst_Instantaneous", "Spawn Count", "18"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                ],
                "material": POLISHED_MATERIALS["arcane_spark"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_LightningSingle",
                "fallback_from": "EM_ElectricArc",
                "as": "HeroLightning",
                "set": [["EmitterState", "Loop Delay", "0.19"]],
                "preserve_renderer": True,
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "ResidualCurl",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.28"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "8"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime Min", "0.8"],
                    ["InitializeParticle", "Lifetime Max", "1.2"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(24,24)"],
                    ["ShapeLocation", "Cylinder Radius", "18"],
                    ["ShapeLocation", "Cylinder Height", "8"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "78"],
                    ["RandomRangeFloat001", "Minimum", "120"],
                    ["RandomRangeFloat001", "Maximum", "260"],
                    ["RandomRangeLinearColor", "Minimum", "(0.04,0.12,0.45,0.7)"],
                    ["RandomRangeLinearColor", "Maximum", "(0.18,0.65,1.0,1)"],
                    ["GravityForce", "Gravity", "(0,0,40)"],
                    ["Drag", "Drag", "2.6"],
                ],
                "material": POLISHED_MATERIALS["arcane_smoke"],
            },
        ],
    },
}


try:
    import revvy_cghow_presets as cghow_presets

    CGHOW_TUTORIAL_PRESETS = cghow_presets.CGHOW_TUTORIAL_PRESETS
    CGHOW_TUTORIAL_PATHS = cghow_presets.PATHS
except ImportError:
    cghow_presets = None
    CGHOW_TUTORIAL_PRESETS = {}
    CGHOW_TUTORIAL_PATHS = {}


def _production_preset_registry():
    registry = dict(PRODUCTION_PRESETS)
    registry.update(CGHOW_TUTORIAL_PRESETS)
    return registry


def ensure_cghow_study_meshes():
    """Create the original portal and tornado study meshes without replacing assets."""
    if cghow_presets is None:
        return {"success": False, "error": "revvy_cghow_presets is unavailable", "meshes": {}}
    results = {
        name: build_procedural_mesh(recipe)
        for name, recipe in cghow_presets.MESH_RECIPES.items()
    }
    return {
        "success": all(result.get("success", False) for result in results.values()),
        "meshes": results,
    }


def _ensure_cghow_material_instance(recipe):
    path = str(recipe["path"])
    parent = unreal.load_asset(str(recipe["parent"]))
    if not parent:
        return {"success": False, "asset_path": path, "error": "parent material is missing"}
    material = unreal.load_asset(path)
    created = False
    if not material:
        folder, name = path.rsplit("/", 1)
        unreal.EditorAssetLibrary.make_directory(folder)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name,
            folder,
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
        created = bool(material)
    if not material:
        return {"success": False, "asset_path": path, "error": "create asset failed"}
    material.modify()
    material.set_editor_property("parent", parent)
    for name, value in (recipe.get("scalars") or {}).items():
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            material, str(name), float(value)
        )
    for name, value in (recipe.get("vectors") or {}).items():
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            material, str(name), unreal.LinearColor(*value)
        )
    for name, value in (recipe.get("textures") or {}).items():
        texture = unreal.load_asset(str(value))
        if not texture:
            return {
                "success": False,
                "asset_path": path,
                "error": "texture parameter asset is missing: %s" % value,
            }
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            material, str(name), texture
        )
    unreal.MaterialEditingLibrary.update_material_instance(material)
    saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return {"success": bool(saved), "asset_path": path, "created": created}


def ensure_cghow_study_materials():
    """Create neutral, archetype-ready material instances for CGHOW studies."""
    if cghow_presets is None:
        return {"success": False, "error": "revvy_cghow_presets is unavailable", "materials": {}}
    results = {
        name: _ensure_cghow_material_instance(recipe)
        for name, recipe in cghow_presets.MATERIAL_RECIPES.items()
    }
    return {
        "success": all(result.get("success", False) for result in results.values()),
        "materials": results,
    }


def cghow_tutorial_catalog():
    """Return the stable preset names and output paths used by the Revvy app."""
    validation = (
        cghow_presets.validate_cghow_presets()
        if cghow_presets is not None
        else {"success": False, "preset_count": 0, "errors": ["module unavailable"]}
    )
    return {
        "validation": validation,
        "presets": [
            {"name": name, "path": CGHOW_TUTORIAL_PATHS[name]}
            for name in sorted(CGHOW_TUTORIAL_PATHS)
        ],
    }


def build_preset(preset, path, tint=None, overrides=None):
    """프리셋 1줄 빌드. tint=첫 블록 틴트 교체, overrides=spec 부분 덮어쓰기(dict.update)."""
    if preset not in PRESETS:
        return f"unknown preset '{preset}'. available: {', '.join(PRESETS)}"
    spec = copy.deepcopy(PRESETS[preset])
    spec["path"] = path
    if tint:
        spec["blocks"][0]["tint"] = tint
    if overrides:
        spec.update(overrides)
    return build(spec)


def build_production_preset(preset, path, tint=None, overrides=None):
    """Build a layered preset and refuse to save until compile and quality gates pass."""
    registry = _production_preset_registry()
    if preset not in registry:
        return f"unknown production preset '{preset}'. available: {', '.join(registry)}"
    if preset in CGHOW_TUTORIAL_PRESETS:
        validation = cghow_presets.validate_cghow_presets()
        if not validation.get("success", False):
            return "CGHOW preset validation failed: " + "; ".join(validation.get("errors", []))
        unknown_overrides = set(overrides or {}) - set(cghow_presets.CGHOW_OVERRIDE_KEYS)
        if unknown_overrides:
            return "unsafe CGHOW override keys: " + ", ".join(sorted(unknown_overrides))
        mesh_result = ensure_cghow_study_meshes()
        if not mesh_result.get("success", False):
            return "CGHOW mesh preparation failed: " + json.dumps(mesh_result, ensure_ascii=False)
    spec = copy.deepcopy(registry[preset])
    spec["path"] = path
    if tint:
        spec["blocks"][0]["tint"] = tint
    if overrides:
        spec.update(overrides)
    result = build(spec)
    if "simulation_failed" in result:
        result += (
            "\nNEXT: run levy_vfx.finalize_production_preset(%r, %r) on the next editor tick"
            % (preset, path)
        )
    return result


def finalize_production_preset(preset, path):
    """Run the post-compile runtime gate on a later editor tick and save only on pass."""
    registry = _production_preset_registry()
    if preset not in registry:
        return f"unknown production preset '{preset}'. available: {', '.join(registry)}"
    spec = copy.deepcopy(registry[preset])
    path = vfx_core.validate_asset_path(path, "NS_")
    runtime_evidence = capture_runtime_evidence(
        path,
        sample_times=spec.get("simulation_samples", (0.05, 0.2, 0.6)),
    )
    report = audit_system(
        path,
        phases=spec.get("phases", []),
        profile=spec.get("profile", "combat"),
        lifecycle=spec.get("lifecycle", ""),
        threshold=int(spec.get("quality_threshold", 78)),
        runtime_evidence=runtime_evidence,
    )
    if not report.get("observation", {}).get("compile", {}).get("success", False):
        report = audit_system(
            path,
            phases=spec.get("phases", []),
            profile=spec.get("profile", "combat"),
            lifecycle=spec.get("lifecycle", ""),
            threshold=int(spec.get("quality_threshold", 78)),
            runtime_evidence=runtime_evidence,
        )
    observation = report.get("observation", {})
    compiled = observation.get("compile", {})
    out = [
        "compile: %s (%s errors, %s warnings)"
        % (
            "OK" if compiled.get("success", False) else "FAIL",
            compiled.get("error_count", 0),
            compiled.get("warning_count", 0),
        ),
        "quality: %s/100 (%s)"
        % (report.get("score", 0), "PASS" if report.get("passed", False) else "FAIL"),
    ]
    for issue in report.get("issues", []):
        out.append(
            "  %s %s: %s"
            % (issue.get("severity", "error").upper(), issue.get("code", "unknown"), issue.get("message", ""))
        )
    passed = bool(compiled.get("success", False) and report.get("passed", False))
    if passed:
        saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        out.append("save: %s" % ("OK" if saved else "FAIL"))
        passed = bool(saved)
    else:
        out.append("save: SKIPPED (fix failures first)")
        if any(issue.get("code") == "simulation_failed" for issue in report.get("issues", [])):
            out.append(
                "NEXT: retry levy_vfx.finalize_production_preset(%r, %r) on the next editor tick"
                % (preset, path)
            )
    out.append("RESULT: %s" % ("PASS" if passed else "FAIL"))
    return "\n".join(out)


def build_cghow_tutorial(preset, overrides=None):
    """Build one CGHOW study at its stable collection path."""
    if preset not in CGHOW_TUTORIAL_PATHS:
        return "unknown CGHOW tutorial preset '%s'. available: %s" % (
            preset,
            ", ".join(sorted(CGHOW_TUTORIAL_PATHS)),
        )
    materials = ensure_cghow_study_materials()
    meshes = ensure_cghow_study_meshes()
    if not materials.get("success") or not meshes.get("success"):
        return "CGHOW support assets: FAIL\nmaterials=%s\nmeshes=%s" % (
            materials,
            meshes,
        )
    return build_production_preset(
        preset,
        CGHOW_TUTORIAL_PATHS[preset],
        overrides=overrides,
    )


def finalize_cghow_tutorial(preset):
    """Run the deferred SimCache gate for one CGHOW study."""
    if preset not in CGHOW_TUTORIAL_PATHS:
        return "unknown CGHOW tutorial preset '%s'. available: %s" % (
            preset,
            ", ".join(sorted(CGHOW_TUTORIAL_PATHS)),
        )
    return finalize_production_preset(preset, CGHOW_TUTORIAL_PATHS[preset])


# ── 이펙트 시안 (Codex 컨셉 이미지) ──────────────────────────────────────
def concept(prompt, name="effect_concept"):
    """이펙트 시안 1장 — Codex 이미지 모델로 생성하고 즉시 이미지 뷰어로 띄움(사용자 확인용).
    텍스처가 아니라 '연출 컨셉 아트' — 임포트하지 않음.

    사용: levy_vfx.concept("dark horror wind magic swirl, green wisps, concept art, game vfx reference")
    반환: 저장된 PNG 경로 (실패 시 에러 문자열)
    """
    import os
    import time
    import unreal
    import revvy_gen_texture as rgt

    project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    out_dir = os.path.join(project_dir, "Saved", "Revvy", "Concepts")
    os.makedirs(out_dir, exist_ok=True)
    out_png = os.path.join(out_dir, f"{name}_{int(time.time())}.png")

    full_prompt = (
        "Game VFX concept art for review (NOT a texture): " + prompt +
        ". Single dramatic presentation shot of the effect in action, dark neutral background, "
        "clear silhouette and color read, production-quality reference."
    )
    ok, msg = rgt.codex_generate_image(full_prompt, out_png, project_dir)
    if not ok or not os.path.exists(out_png):
        return f"CONCEPT FAIL: {msg}"
    try:
        os.startfile(out_png)  # 기본 이미지 뷰어로 표시
    except Exception:
        pass
    return out_png


# ── MI 파생 (사람-조립 워크플로우의 재료 공급용) ─────────────────────────
def derive_mi(name, master="Additive", folder="/Game/VFX/Materials",
              texture=None, tint=None, emissive=None, params=None):
    """마스터에서 MI 1개 파생 + 파라미터 설정. 1콜.

    master: "Additive" | "Translucent" | "Erosion" (또는 풀 경로)
    texture: 텍스처 에셋 경로 ("Texture" 파라미터)
    tint: (R,G,B[,A]) 튜플 또는 "(R,G,B)" 문자열
    emissive: float (EmissiveIntensity)
    params: {스칼라파라미터명: float} 추가 설정
    반환: MI 경로 또는 에러 문자열
    """
    import unreal
    MASTERS = {
        "Additive": "/Revvy/VFX/Materials/M_LevyVFX_Additive",
        "Translucent": "/Revvy/VFX/Materials/M_LevyVFX_Translucent",
        "Erosion": "/Revvy/VFX/Materials/M_LevyVFX_Erosion",
        "PolishedSprite": "/Revvy/VFX/Materials/M_RevvyVFX_PolishedSprite",
        "PolishedSmoke": "/Revvy/VFX/Materials/M_RevvyVFX_PolishedSmoke",
        "PolishedRing": "/Revvy/VFX/Materials/M_RevvyVFX_PolishedRing",
        "PolishedMesh": "/Revvy/VFX/Materials/M_RevvyVFX_PolishedMesh",
    }
    master_path = MASTERS.get(master, master)
    parent = unreal.load_asset(master_path)
    if not parent:
        return f"FAIL: master not found {master_path}"

    dst = f"{folder}/{name}"
    mi = unreal.load_asset(dst)
    if not mi:
        atools = unreal.AssetToolsHelpers.get_asset_tools()
        mi = atools.create_asset(name, folder, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
        if not mi:
            return f"FAIL: create {dst}"
        mi.set_editor_property("parent", parent)

    MEL = unreal.MaterialEditingLibrary
    if texture:
        tex = unreal.load_asset(texture)
        if tex:
            MEL.set_material_instance_texture_parameter_value(mi, "Texture", tex)
    if tint:
        if isinstance(tint, str):
            tint = tint.strip("() ").split(",")
        v = [float(x) for x in tint] + [1.0] * (4 - len(tint))
        MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(*v[:4]))
    if emissive is not None:
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveIntensity", float(emissive))
    for k, val in (params or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(val))

    unreal.EditorAssetLibrary.save_asset(dst, only_if_is_dirty=False)
    return dst

