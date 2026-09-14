"""Build and capture a deterministic Unreal VFX validation lab."""

import math
import os

import unreal


LAB_MAP = "/Game/VFX/Validation/Maps/L_RevvyVFXValidation"
LAB_MATERIAL_DIR = "/Game/VFX/Validation/Materials"
LAB_CAPTURE_DIR = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
    "Revvy",
    "Validation",
    "VFXLab",
)

MATERIALS = {
    "dark": LAB_MATERIAL_DIR + "/MI_RevvyVFXLab_Dark",
    "gray": LAB_MATERIAL_DIR + "/MI_RevvyVFXLab_Gray18",
    "light": LAB_MATERIAL_DIR + "/MI_RevvyVFXLab_Light",
    "floor": LAB_MATERIAL_DIR + "/MI_RevvyVFXLab_Floor",
    "pedestal": LAB_MATERIAL_DIR + "/MI_RevvyVFXLab_Pedestal",
}

EFFECTS = {
    "impact": {
        "label": "VFX_Impact",
        "camera": "CAM_Impact",
        "path": "/Game/VFX/Generated/CGHOW/Impact/NS_Revvy_CGHOWStudy_Impact_V03",
        "location": (-1200.0, 100.0, 60.0),
        "target_z": 110.0,
        "camera_distance": 600.0,
        "camera_height": 200.0,
        "seed": 101,
        "phases": {"early": 0.055, "peak": 0.16, "decay": 0.42},
    },
    "portal": {
        "label": "VFX_Portal",
        "camera": "CAM_Portal",
        "path": "/Game/VFX/Generated/CGHOW/Portal/NS_Revvy_CGHOWStudy_Portal_V02",
        "location": (-600.0, 100.0, 180.0),
        "target_z": 250.0,
        "camera_distance": 720.0,
        "camera_height": 70.0,
        "seed": 202,
        "phases": {"early": 0.12, "peak": 0.52, "decay": 2.60},
    },
    "lightning": {
        "label": "VFX_Lightning",
        "camera": "CAM_Lightning",
        "path": "/Game/VFX/Generated/CGHOW/Lightning/NS_Revvy_CGHOWStudy_Lightning_V05",
        "location": (0.0, 100.0, 60.0),
        "target_z": 230.0,
        "camera_distance": 850.0,
        "camera_height": 90.0,
        "seed": 303,
        "phases": {"early": 0.08, "peak": 0.24, "decay": 0.52},
    },
    "water": {
        "label": "VFX_Water",
        "camera": "CAM_Water",
        "path": "/Game/VFX/Generated/CGHOW/Water/NS_Revvy_CGHOWStudy_WaterSplash_V04",
        "location": (600.0, 100.0, 60.0),
        "target_z": 140.0,
        "camera_distance": 650.0,
        "camera_height": 190.0,
        "seed": 404,
        "phases": {"early": 0.06, "peak": 0.11, "decay": 0.50},
    },
    "tornado": {
        "label": "VFX_Tornado",
        "camera": "CAM_Tornado",
        "path": "/Game/VFX/Generated/CGHOW/Tornado/NS_Revvy_CGHOWStudy_DarkTornado_V02",
        "location": (1200.0, 100.0, 45.0),
        "target_z": 235.0,
        "camera_distance": 560.0,
        "camera_height": 90.0,
        "seed": 505,
        "phases": {"early": 0.24, "peak": 0.90, "decay": 2.90},
    },
}

_CAPTURE_TASKS = {}
_ENVIRONMENT_MODES = ("presentation", "neutral")
_SKY_SPHERE_MESH = "/Engine/EngineSky/SM_SkySphere"
_ENVIRONMENT_STATE_TAG_PREFIX = "RevvyVFXLabEnvState:"
_PRESENTATION_ENVIRONMENT_STATE = {}
_ENVIRONMENT_MODE = "presentation"
_SCENE_CAPTURE_LABEL = "VFXLab_SceneCapture"
_SCENE_CAPTURE_PREPARED = None
_SCENE_CAPTURE_LAST_EXPORT = None


def _actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _level_subsystem():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _actors_by_label():
    return {actor.get_actor_label(): actor for actor in _actor_subsystem().get_all_level_actors()}


def _asset_package_path(asset):
    if not asset:
        return ""
    return str(asset.get_path_name()).split(".", 1)[0]


def _environment_actors():
    """Query only atmospheric actors that were verified in the validation map."""
    found = []
    for actor in _actor_subsystem().get_all_level_actors():
        role = ""
        if isinstance(actor, unreal.ExponentialHeightFog):
            role = "fog"
        elif isinstance(actor, unreal.VolumetricCloud):
            role = "cloud"
        elif isinstance(actor, unreal.SkyAtmosphere):
            role = "sky_atmosphere"
        elif isinstance(actor, unreal.StaticMeshActor):
            component = actor.get_component_by_class(unreal.StaticMeshComponent)
            mesh = component.get_editor_property("static_mesh") if component else None
            if _asset_package_path(mesh) == _SKY_SPHERE_MESH:
                role = "sky_sphere"
        if role:
            found.append((role, actor))
    return sorted(found, key=lambda item: str(item[1].get_path_name()))


def _environment_actor_state(actor):
    return {
        "hidden_in_game": bool(actor.get_editor_property("hidden")),
        "temporarily_hidden_in_editor": bool(actor.is_temporarily_hidden_in_editor()),
    }


def _apply_environment_actor_state(actor, state):
    actor.set_actor_hidden_in_game(bool(state["hidden_in_game"]))
    actor.set_is_temporarily_hidden_in_editor(bool(state["temporarily_hidden_in_editor"]))


def _environment_state_tag(actor):
    for value in actor.get_editor_property("tags"):
        text = str(value)
        if text.startswith(_ENVIRONMENT_STATE_TAG_PREFIX):
            values = text[len(_ENVIRONMENT_STATE_TAG_PREFIX):].split(",")
            if len(values) == 2:
                return {
                    "hidden_in_game": values[0] == "1",
                    "temporarily_hidden_in_editor": values[1] == "1",
                }
    return None


def _set_environment_state_tag(actor, state):
    tags = [
        value
        for value in actor.get_editor_property("tags")
        if not str(value).startswith(_ENVIRONMENT_STATE_TAG_PREFIX)
    ]
    if state is not None:
        tags.append(
            unreal.Name(
                _ENVIRONMENT_STATE_TAG_PREFIX
                + ("1" if state["hidden_in_game"] else "0")
                + ","
                + ("1" if state["temporarily_hidden_in_editor"] else "0")
            )
        )
    actor.set_editor_property("tags", tags)


def set_environment(mode="presentation"):
    """Toggle atmospheric presentation actors without deleting or editing them."""
    global _ENVIRONMENT_MODE
    if mode not in _ENVIRONMENT_MODES:
        raise ValueError("Environment mode must be presentation or neutral")
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")

    actors = _environment_actors()
    changed = []
    if mode == "neutral":
        for role, actor in actors:
            actor_path = str(actor.get_path_name())
            if actor_path not in _PRESENTATION_ENVIRONMENT_STATE:
                state = _environment_state_tag(actor) or _environment_actor_state(actor)
                _PRESENTATION_ENVIRONMENT_STATE[actor_path] = state
                _set_environment_state_tag(actor, state)
            actor.set_actor_hidden_in_game(True)
            actor.set_is_temporarily_hidden_in_editor(True)
            changed.append({"role": role, "label": actor.get_actor_label()})
    else:
        for role, actor in actors:
            actor_path = str(actor.get_path_name())
            state = _PRESENTATION_ENVIRONMENT_STATE.pop(actor_path, None) or _environment_state_tag(actor)
            if state is not None:
                _apply_environment_actor_state(actor, state)
                _set_environment_state_tag(actor, None)
                changed.append({"role": role, "label": actor.get_actor_label()})

    _ENVIRONMENT_MODE = mode
    _level_subsystem().editor_invalidate_viewports()
    return {"mode": mode, "actors": changed}


def environment_status():
    """Report queried atmospheric actors and their current visibility state."""
    if not _ensure_lab_loaded():
        return {"loaded": False, "mode": _ENVIRONMENT_MODE, "actors": []}
    actors = []
    for role, actor in _environment_actors():
        state = _environment_actor_state(actor)
        state.update({"role": role, "label": actor.get_actor_label()})
        actors.append(state)
    return {"loaded": True, "mode": _ENVIRONMENT_MODE, "actors": actors}


def _safe_filename_tag(value):
    raw = str(value or "").strip()
    if not raw:
        return ""
    tag = "".join(character if character.isalnum() or character in "-_" else "_" for character in raw)
    tag = tag.strip("_")[:64]
    if not tag:
        raise ValueError("Capture filename tag has no filename-safe characters")
    return tag


def _capture_filename(parts, environment, filename_tag):
    tag = _safe_filename_tag(filename_tag)
    if environment != "presentation":
        parts = list(parts) + [environment]
    if tag:
        if not parts or parts[-1] != tag:
            parts = list(parts) + [tag]
    return "_".join(parts) + ".png"


def _ensure_lab_loaded():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world and str(world.get_path_name()).split(".")[0] == LAB_MAP:
        return True
    return bool(_level_subsystem().load_level(LAB_MAP))


def _scene_capture_actor(create=False):
    matches = [
        actor
        for actor in _actor_subsystem().get_all_level_actors()
        if actor.get_actor_label() == _SCENE_CAPTURE_LABEL
    ]
    captures = [actor for actor in matches if isinstance(actor, unreal.SceneCapture2D)]
    if captures:
        return sorted(captures, key=lambda actor: str(actor.get_path_name()))[0], False
    if matches:
        raise RuntimeError(_SCENE_CAPTURE_LABEL + " exists but is not a SceneCapture2D actor")
    if not create:
        return None, False

    actor = _actor_subsystem().spawn_actor_from_class(
        unreal.SceneCapture2D,
        unreal.Vector(),
        unreal.Rotator(),
        False,
    )
    if not actor:
        raise RuntimeError("Failed to create SceneCapture2D actor")
    actor.set_actor_label(_SCENE_CAPTURE_LABEL)
    return actor, True


def _scene_capture_component(actor):
    component = actor.get_component_by_class(unreal.SceneCaptureComponent2D) if actor else None
    if not component:
        raise RuntimeError(_SCENE_CAPTURE_LABEL + " has no SceneCaptureComponent2D")
    return component


def _validate_capture_dimensions(width, height):
    width = int(width)
    height = int(height)
    if not 16 <= width <= 8192 or not 16 <= height <= 8192:
        raise ValueError("Scene capture dimensions must be between 16 and 8192")
    return width, height


def _ensure_scene_capture_target(component, width=1024, height=1024):
    width, height = _validate_capture_dimensions(width, height)
    target = component.get_editor_property("texture_target")
    expected_format = unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB
    if not target or target.get_editor_property("render_target_format") != expected_format:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        target = unreal.RenderingLibrary.create_render_target2d(
            world,
            width,
            height,
            expected_format,
            unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
            False,
            False,
        )
        if not target:
            raise RuntimeError("Failed to create SceneCapture2D render target")
        component.set_editor_property("texture_target", target)
    elif target.get_editor_property("size_x") != width or target.get_editor_property("size_y") != height:
        unreal.RenderingLibrary.resize_render_target2d(target, width, height)
    return target


def _configure_scene_capture(actor, width=1024, height=1024):
    component = _scene_capture_component(actor)
    component.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    component.set_editor_property("capture_every_frame", False)
    component.set_editor_property("capture_on_movement", False)
    component.set_editor_property("projection_type", unreal.CameraProjectionMode.PERSPECTIVE)
    target = _ensure_scene_capture_target(component, width, height)
    return component, target


def _seek_component(component, seed, desired_age):
    tick_delta = 1.0 / 60.0
    tick_count = max(1, int(math.ceil(float(desired_age) / tick_delta)))
    component.set_editor_property("random_seed_offset", int(seed))
    component.set_force_solo(True)
    component.set_can_render_while_seeking(True)
    component.set_seek_delta(tick_delta)
    component.set_lock_desired_age_delta_time_to_seek_delta(True)
    component.set_age_update_mode(unreal.NiagaraAgeUpdateMode.TICK_DELTA_TIME)
    component.reinitialize_system()
    component.advance_simulation(tick_count, tick_delta)
    component.set_age_update_mode(unreal.NiagaraAgeUpdateMode.DESIRED_AGE)
    component.set_desired_age(tick_count * tick_delta)


def _ensure_material(name, color, roughness):
    path = MATERIALS[name]
    material = unreal.load_asset(path)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            path.rsplit("/", 1)[-1],
            LAB_MATERIAL_DIR,
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
    if not material:
        raise RuntimeError("Failed to create material: " + path)
    parent = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    material.set_editor_property("parent", parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        material,
        "Color",
        unreal.LinearColor(*color),
    )
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        material,
        "Roughness",
        float(roughness),
    )
    unreal.MaterialEditingLibrary.update_material_instance(material)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return material


def _ensure_lab_materials():
    return {
        "dark": _ensure_material("dark", (0.006, 0.009, 0.014, 1.0), 0.96),
        "gray": _ensure_material("gray", (0.18, 0.18, 0.18, 1.0), 0.92),
        "light": _ensure_material("light", (0.72, 0.72, 0.72, 1.0), 0.88),
        "floor": _ensure_material("floor", (0.025, 0.032, 0.045, 1.0), 0.94),
        "pedestal": _ensure_material("pedestal", (0.08, 0.095, 0.12, 1.0), 0.82),
    }


def _spawn_mesh(label, mesh, location, scale, material, rotation=None):
    actor = _actor_subsystem().spawn_actor_from_object(
        mesh,
        unreal.Vector(*location),
        rotation or unreal.Rotator(),
        False,
    )
    if not actor:
        raise RuntimeError("Failed to spawn mesh actor: " + label)
    actor.set_actor_label(label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    component.set_material(0, material)
    return actor


def _spawn_camera(label, location, target, fov=42.0):
    start = unreal.Vector(*location)
    rotation = unreal.MathLibrary.find_look_at_rotation(start, unreal.Vector(*target))
    actor = _actor_subsystem().spawn_actor_from_class(
        unreal.CameraActor,
        start,
        rotation,
        False,
    )
    if not actor:
        raise RuntimeError("Failed to spawn camera: " + label)
    actor.set_actor_label(label)
    actor.get_editor_property("camera_component").set_editor_property("field_of_view", float(fov))
    return actor


def _configure_post_process(actor):
    actor.set_editor_property("unbound", True)
    settings = actor.get_editor_property("settings")
    values = {
        "override_auto_exposure_min_brightness": True,
        "override_auto_exposure_max_brightness": True,
        "auto_exposure_min_brightness": 1.0,
        "auto_exposure_max_brightness": 1.0,
        "override_bloom_intensity": True,
        "bloom_intensity": 0.35,
        "override_vignette_intensity": True,
        "vignette_intensity": 0.0,
    }
    for name, value in values.items():
        settings.set_editor_property(name, value)
    actor.set_editor_property("settings", settings)


def build_lab():
    """Populate the existing validation map without deleting any actors or assets."""
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available: " + LAB_MAP)

    missing = [spec["path"] for spec in EFFECTS.values() if not unreal.EditorAssetLibrary.does_asset_exist(spec["path"])]
    if missing:
        raise RuntimeError("Missing Niagara systems: " + ", ".join(missing))

    materials = _ensure_lab_materials()
    actors = _actors_by_label()
    created = []

    floor = actors.get("Floor")
    if not floor:
        raise RuntimeError("Template floor is missing")
    floor_component = floor.get_component_by_class(unreal.StaticMeshComponent)
    floor_component.set_material(0, materials["floor"])

    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    cylinder = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    if not cube or not cylinder:
        raise RuntimeError("Engine validation meshes are missing")

    if "VFXLab_Backdrop" not in actors:
        _spawn_mesh(
            "VFXLab_Backdrop",
            cube,
            (0.0, 650.0, 450.0),
            (36.0, 0.25, 9.0),
            materials["dark"],
        )
        created.append("VFXLab_Backdrop")

    for key, spec in EFFECTS.items():
        x, y, _ = spec["location"]
        pedestal_label = "Pedestal_" + key.capitalize()
        if pedestal_label not in actors:
            _spawn_mesh(
                pedestal_label,
                cylinder,
                (x, y, 20.0),
                (2.15, 2.15, 0.40),
                materials["pedestal"],
            )
            created.append(pedestal_label)
        scale_label = "Scale180_" + key.capitalize()
        if scale_label not in actors:
            _spawn_mesh(
                scale_label,
                cube,
                (x - 230.0, y + 90.0, 90.0),
                (0.12, 0.12, 1.80),
                materials["gray"],
            )
            created.append(scale_label)

        system = unreal.load_asset(spec["path"])
        effect_actor = actors.get(spec["label"])
        if not effect_actor:
            effect_actor = _actor_subsystem().spawn_actor_from_object(
                system,
                unreal.Vector(*spec["location"]),
                unreal.Rotator(),
                False,
            )
            if not effect_actor:
                raise RuntimeError("Failed to spawn Niagara actor: " + spec["label"])
            effect_actor.set_actor_label(spec["label"])
            effect_actor.set_destroy_on_system_finish(False)
            component = effect_actor.get_editor_property("niagara_component")
            component.set_editor_property("allow_scalability", False)
            _seek_component(component, spec["seed"], spec["phases"]["peak"])
            created.append(spec["label"])
        component = effect_actor.get_editor_property("niagara_component")
        if component.get_asset() != system:
            component.set_editor_property("asset", system)
        component.set_editor_property("allow_scalability", False)
        component.set_editor_property("random_seed_offset", int(spec["seed"]))
        effect_actor.set_actor_location(unreal.Vector(*spec["location"]), False, False)
        _seek_component(component, spec["seed"], spec["phases"]["peak"])

        distance = float(spec["camera_distance"])
        camera_location = (
            x,
            y - distance,
            float(spec["target_z"]) + float(spec.get("camera_height", 85.0)),
        )
        camera_target = (x, y, float(spec["target_z"]))
        camera = actors.get(spec["camera"])
        if not camera:
            camera = _spawn_camera(spec["camera"], camera_location, camera_target, 42.0)
            created.append(spec["camera"])
        else:
            start = unreal.Vector(*camera_location)
            camera.set_actor_location(start, False, False)
            camera.set_actor_rotation(
                unreal.MathLibrary.find_look_at_rotation(start, unreal.Vector(*camera_target)),
                False,
            )
            camera.get_editor_property("camera_component").set_editor_property("field_of_view", 42.0)

    if "CAM_Overview" not in actors:
        _spawn_camera(
            "CAM_Overview",
            (0.0, -2750.0, 920.0),
            (0.0, 100.0, 210.0),
            48.0,
        )
        created.append("CAM_Overview")

    actors = _actors_by_label()
    post_process = actors.get("PPV_RevvyVFXLab")
    if not post_process:
        post_process = _actor_subsystem().spawn_actor_from_class(
            unreal.PostProcessVolume,
            unreal.Vector(0.0, 0.0, 0.0),
            unreal.Rotator(),
            False,
        )
        post_process.set_actor_label("PPV_RevvyVFXLab")
        created.append("PPV_RevvyVFXLab")
    _configure_post_process(post_process)

    scene_capture, scene_capture_created = _scene_capture_actor(create=True)
    _configure_scene_capture(scene_capture, 1024, 1024)
    if scene_capture_created:
        created.append(_SCENE_CAPTURE_LABEL)

    saved = _level_subsystem().save_current_level()
    return {
        "map": LAB_MAP,
        "created": created,
        "actor_count": len(_actor_subsystem().get_all_level_actors()),
        "saved": bool(saved),
    }


def set_backdrop(mode="dark"):
    """Switch the validation wall between controlled dark, gray, and light modes."""
    if mode not in ("dark", "gray", "light"):
        raise ValueError("Backdrop mode must be dark, gray, or light")
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")
    actor = _actors_by_label().get("VFXLab_Backdrop")
    material = unreal.load_asset(MATERIALS[mode])
    if not actor or not material:
        raise RuntimeError("Validation backdrop is incomplete")
    actor.get_component_by_class(unreal.StaticMeshComponent).set_material(0, material)
    return mode


def _hide_all_niagara_components():
    """Prevent preview, Sim Cache, or manually placed Niagara from contaminating a capture."""
    hidden = []
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        for component in actor.get_components_by_class(unreal.NiagaraComponent):
            if component.is_visible():
                component.set_visibility(False, True)
                hidden.append(actor.get_actor_label() + "." + component.get_name())
    return hidden


def prepare_frame(effect_key, phase="peak", backdrop="dark", visible_only=True, environment="presentation"):
    """Seek one Niagara actor to a deterministic review frame."""
    if effect_key not in EFFECTS:
        raise KeyError("Unknown effect: " + str(effect_key))
    spec = EFFECTS[effect_key]
    if phase not in spec["phases"]:
        raise KeyError("Unknown phase: " + str(phase))
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")
    set_environment(environment)
    set_backdrop(backdrop)
    isolated_components = _hide_all_niagara_components() if visible_only else []
    actors = _actors_by_label()
    for key, candidate in EFFECTS.items():
        actor = actors.get(candidate["label"])
        if not actor:
            raise RuntimeError("Missing lab effect actor: " + candidate["label"])
        visible = key == effect_key or not visible_only
        actor.set_actor_hidden_in_game(not visible)
        component = actor.get_editor_property("niagara_component")
        component.set_visibility(visible, True)
        if visible:
            _seek_component(component, spec["seed"], spec["phases"][phase])
    return {
        "effect": effect_key,
        "phase": phase,
        "age": float(spec["phases"][phase]),
        "camera": spec["camera"],
        "backdrop": backdrop,
        "environment": environment,
        "isolated_components": isolated_components,
    }


def prepare_blank(effect_key, backdrop="dark", environment="presentation"):
    """Hide all Niagara actors while keeping an effect camera for background differencing."""
    if effect_key not in EFFECTS:
        raise KeyError("Unknown effect: " + str(effect_key))
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")
    set_environment(environment)
    set_backdrop(backdrop)
    isolated_components = _hide_all_niagara_components()
    actors = _actors_by_label()
    for spec in EFFECTS.values():
        actor = actors.get(spec["label"])
        if actor:
            actor.set_actor_hidden_in_game(True)
            actor.get_editor_property("niagara_component").set_visibility(False, True)
    return {
        "effect": effect_key,
        "camera": EFFECTS[effect_key]["camera"],
        "backdrop": backdrop,
        "environment": environment,
        "isolated_components": isolated_components,
    }


def prepare_scene_capture(
    effect_key,
    phase="peak",
    backdrop="dark",
    blank=False,
    environment="presentation",
):
    """Prepare deterministic state and position the SceneCapture2D rig.

    Callers must allow at least one editor tick after this function returns
    before calling export_prepared_scene_capture. The path is independent of
    foreground windows, active editor tabs, and level viewport focus.
    """
    global _SCENE_CAPTURE_PREPARED
    state = (
        prepare_blank(effect_key, backdrop, environment)
        if blank
        else prepare_frame(effect_key, phase, backdrop, True, environment)
    )
    actors = _actors_by_label()
    camera = actors.get(state["camera"])
    if not camera:
        raise RuntimeError("Missing capture camera: " + state["camera"])
    scene_capture, _ = _scene_capture_actor(create=False)
    if not scene_capture:
        raise RuntimeError("Missing " + _SCENE_CAPTURE_LABEL + "; call build_lab first")

    component, target = _configure_scene_capture(scene_capture, 1024, 1024)
    scene_capture.set_actor_location(camera.get_actor_location(), False, False)
    scene_capture.set_actor_rotation(camera.get_actor_rotation(), False)
    camera_component = camera.get_editor_property("camera_component")
    component.set_editor_property("fov_angle", float(camera_component.get_editor_property("field_of_view")))
    _level_subsystem().editor_invalidate_viewports()

    _SCENE_CAPTURE_PREPARED = {
        "effect": effect_key,
        "phase": "blank" if blank else phase,
        "backdrop": backdrop,
        "blank": bool(blank),
        "environment": environment,
        "camera": state["camera"],
        "capture_actor": scene_capture.get_actor_label(),
        "capture_source": str(component.get_editor_property("capture_source")),
        "render_target": str(target.get_path_name()),
        "wait_editor_ticks": 1,
    }
    return dict(_SCENE_CAPTURE_PREPARED)


def export_prepared_scene_capture(
    effect_key,
    phase="peak",
    backdrop="dark",
    blank=False,
    environment="presentation",
    width=1024,
    height=1024,
    filename_tag="",
):
    """Export a previously prepared SceneCapture2D frame as an sRGB PNG.

    prepare_scene_capture must have run with matching arguments at least one
    editor tick earlier. This function never depends on the foreground window,
    active asset tab, or level viewport camera.
    """
    global _SCENE_CAPTURE_LAST_EXPORT
    expected = {
        "effect": effect_key,
        "phase": "blank" if blank else phase,
        "backdrop": backdrop,
        "blank": bool(blank),
        "environment": environment,
    }
    if not _SCENE_CAPTURE_PREPARED:
        raise RuntimeError("Scene capture is not prepared; call prepare_scene_capture and wait one editor tick")
    mismatches = [
        name
        for name, value in expected.items()
        if _SCENE_CAPTURE_PREPARED.get(name) != value
    ]
    if mismatches:
        raise RuntimeError(
            "Prepared scene capture does not match: "
            + ", ".join(mismatches)
            + "; prepare again and wait one editor tick"
        )
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")

    width, height = _validate_capture_dimensions(width, height)
    scene_capture, _ = _scene_capture_actor(create=False)
    if not scene_capture:
        raise RuntimeError("Missing " + _SCENE_CAPTURE_LABEL + "; call build_lab first")
    component, target = _configure_scene_capture(scene_capture, width, height)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.RenderingLibrary.clear_render_target2d(
        world,
        target,
        unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
    )
    component.capture_scene()

    os.makedirs(LAB_CAPTURE_DIR, exist_ok=True)
    custom_tag = _safe_filename_tag(filename_tag)
    render_tag = "rt" + (("_" + custom_tag) if custom_tag else "")
    suffix = "blank" if blank else phase
    filename = _capture_filename((effect_key, suffix, backdrop), environment, render_tag)
    output_path = os.path.join(LAB_CAPTURE_DIR, filename)
    unreal.RenderingLibrary.export_render_target(world, target, LAB_CAPTURE_DIR, filename)
    if not os.path.isfile(output_path):
        raise RuntimeError("Scene capture export did not create: " + output_path)

    _SCENE_CAPTURE_LAST_EXPORT = {
        "path": output_path,
        "filename": filename,
        "width": width,
        "height": height,
        "format": str(target.get_editor_property("render_target_format")),
        "capture_source": str(component.get_editor_property("capture_source")),
        "prepared": dict(_SCENE_CAPTURE_PREPARED),
    }
    return dict(_SCENE_CAPTURE_LAST_EXPORT)


def capture(
    effect_key,
    phase="peak",
    backdrop="dark",
    blank=False,
    width=1024,
    height=1024,
    environment="presentation",
    filename_tag="",
):
    """Queue a UI-free deterministic screenshot and return its absolute output path."""
    state = (
        prepare_blank(effect_key, backdrop, environment)
        if blank
        else prepare_frame(effect_key, phase, backdrop, True, environment)
    )
    actors = _actors_by_label()
    camera = actors.get(state["camera"])
    if not camera:
        raise RuntimeError("Missing capture camera: " + state["camera"])
    os.makedirs(LAB_CAPTURE_DIR, exist_ok=True)
    suffix = "blank" if blank else phase
    filename = _capture_filename((effect_key, suffix, backdrop), environment, filename_tag)
    output_path = os.path.join(LAB_CAPTURE_DIR, filename)
    task = unreal.AutomationLibrary.take_high_res_screenshot(
        int(width),
        int(height),
        output_path,
        camera,
        False,
        False,
        unreal.ComparisonTolerance.LOW,
        "Revvy deterministic VFX validation",
        0.20,
        True,
    )
    _CAPTURE_TASKS[filename] = task
    return {"path": output_path, "task": str(task), "state": state}


def capture_overview(width=1280, height=720, environment="presentation", filename_tag=""):
    """Queue a UI-free overview capture with all effects held at their peak frame."""
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")
    set_environment(environment)
    set_backdrop("dark")
    _hide_all_niagara_components()
    actors = _actors_by_label()
    for spec in EFFECTS.values():
        actor = actors.get(spec["label"])
        if not actor:
            raise RuntimeError("Missing lab effect actor: " + spec["label"])
        actor.set_actor_hidden_in_game(False)
        component = actor.get_editor_property("niagara_component")
        component.set_visibility(True, True)
        _seek_component(component, spec["seed"], spec["phases"]["peak"])
    camera = actors.get("CAM_Overview")
    if not camera:
        raise RuntimeError("Missing overview camera")
    os.makedirs(LAB_CAPTURE_DIR, exist_ok=True)
    filename = _capture_filename(("overview", "peak"), environment, filename_tag)
    output_path = os.path.join(LAB_CAPTURE_DIR, filename)
    task = unreal.AutomationLibrary.take_high_res_screenshot(
        int(width),
        int(height),
        output_path,
        camera,
        False,
        False,
        unreal.ComparisonTolerance.LOW,
        "Revvy VFX lab overview",
        0.20,
        True,
    )
    _CAPTURE_TASKS[filename] = task
    return {"path": output_path, "task": str(task), "environment": environment}


def capture_task_status(filename):
    """Return lifetime and completion state for a queued editor screenshot."""
    task = _CAPTURE_TASKS.get(filename)
    if not task:
        return {"known": False, "filename": filename}
    return {
        "known": True,
        "filename": filename,
        "valid": bool(task.is_valid_task()),
        "done": bool(task.is_task_done()),
    }


def scene_capture_status():
    """Report SceneCapture2D readiness without relying on editor UI state."""
    if not _ensure_lab_loaded():
        return {"loaded": False, "label": _SCENE_CAPTURE_LABEL}
    actor, _ = _scene_capture_actor(create=False)
    if not actor:
        return {"loaded": True, "exists": False, "label": _SCENE_CAPTURE_LABEL}
    component = _scene_capture_component(actor)
    target = component.get_editor_property("texture_target")
    return {
        "loaded": True,
        "exists": True,
        "label": actor.get_actor_label(),
        "actor_path": str(actor.get_path_name()),
        "capture_source": str(component.get_editor_property("capture_source")),
        "capture_every_frame": bool(component.get_editor_property("capture_every_frame")),
        "capture_on_movement": bool(component.get_editor_property("capture_on_movement")),
        "render_target": str(target.get_path_name()) if target else "",
        "render_target_size": (
            int(target.get_editor_property("size_x")),
            int(target.get_editor_property("size_y")),
        ) if target else (0, 0),
        "render_target_format": str(target.get_editor_property("render_target_format")) if target else "",
        "prepared": dict(_SCENE_CAPTURE_PREPARED) if _SCENE_CAPTURE_PREPARED else None,
        "last_export": dict(_SCENE_CAPTURE_LAST_EXPORT) if _SCENE_CAPTURE_LAST_EXPORT else None,
    }


def restore_live_preview():
    """Restore all effects to their authored tick mode for interactive level review."""
    global _SCENE_CAPTURE_PREPARED
    if not _ensure_lab_loaded():
        raise RuntimeError("Validation map is not available")
    set_environment("presentation")
    _hide_all_niagara_components()
    actors = _actors_by_label()
    restored = []
    for spec in EFFECTS.values():
        actor = actors.get(spec["label"])
        if not actor:
            continue
        actor.set_actor_hidden_in_game(False)
        component = actor.get_editor_property("niagara_component")
        component.set_visibility(True, True)
        component.set_force_solo(False)
        component.set_age_update_mode(unreal.NiagaraAgeUpdateMode.TICK_DELTA_TIME)
        component.reinitialize_system()
        restored.append(spec["label"])
    _SCENE_CAPTURE_PREPARED = None
    return restored


def lab_status():
    if not _ensure_lab_loaded():
        return {"loaded": False, "map": LAB_MAP}
    actors = _actors_by_label()
    return {
        "loaded": True,
        "map": LAB_MAP,
        "actor_count": len(actors),
        "effects": {key: spec["label"] in actors for key, spec in EFFECTS.items()},
        "cameras": {key: spec["camera"] in actors for key, spec in EFFECTS.items()},
        "overview_camera": "CAM_Overview" in actors,
        "post_process": "PPV_RevvyVFXLab" in actors,
        "backdrop": "VFXLab_Backdrop" in actors,
        "environment": environment_status(),
        "scene_capture": scene_capture_status(),
    }
