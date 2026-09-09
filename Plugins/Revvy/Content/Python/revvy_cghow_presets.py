"""CGHOW-inspired Niagara study presets for Revvy.

These are original study assets assembled from public tutorial principles and
verified local emitter templates. They are not CGHOW project-file replicas.
"""

from __future__ import annotations

from copy import deepcopy


ROOT = "/Game/VFX/Generated/CGHOW"
MATERIAL_ROOT = "/Revvy/VFX/Materials"
STUDY_MATERIAL_ROOT = ROOT + "/Materials"

MATERIALS = {
    "core": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_GlowNeutral",
    "streak": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_SparkNeutral",
    "wisp": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_ArcaneSmokeNeutral",
    "rune": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_RunicNeutral",
    "smoke": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_SmokeNeutral",
    "ring": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_RingNeutral",
    "mesh": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_RingNeutral",
    "tornado_guide": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_TornadoGuide",
    "dark_core": MATERIAL_ROOT + "/MI_RevvyPolishedDarkCore",
    "lightning": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_Lightning",
    "tornado_smoke": STUDY_MATERIAL_ROOT + "/MI_Revvy_CGHOW_TornadoSmoke",
}

MATERIAL_RECIPES = {
    "core": {
        "path": MATERIALS["core"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedGlow",
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "streak": {
        "path": MATERIALS["streak"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedSpark",
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "wisp": {
        "path": MATERIALS["wisp"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedArcaneSmoke",
        "scalars": {"OpacityScale": 0.28},
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "rune": {
        "path": MATERIALS["rune"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedRunic",
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "smoke": {
        "path": MATERIALS["smoke"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedSmoke",
        "scalars": {"OpacityScale": 0.38, "EmissiveIntensity": 0.22},
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "ring": {
        "path": MATERIALS["ring"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedRing",
        "scalars": {"EmissiveIntensity": 1.7, "ExpansionAmount": 1.4},
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "tornado_guide": {
        "path": MATERIALS["tornado_guide"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedRing",
        "scalars": {
            "EmissiveIntensity": 0.45,
            "ExpansionAmount": 0.45,
            "FresnelFloor": 0.08,
        },
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
    "lightning": {
        "path": MATERIALS["lightning"],
        "parent": "/Game/SoStylized/Effects/Materials/MI_LightningStrike",
        "scalars": {
            "Glow Brightness": 5.0,
            "Glow Multiplier": 0.50,
            "WPO Strength": 3.0,
            "Width Tightening": 3.6,
        },
    },
    "tornado_smoke": {
        "path": MATERIALS["tornado_smoke"],
        "parent": MATERIAL_ROOT + "/MI_RevvyPolishedArcaneSmoke",
        "scalars": {"OpacityScale": 0.50, "EmissiveIntensity": 0.60},
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
    },
}

MESHES = {
    "portal_ring": ROOT + "/Meshes/SM_Revvy_CGHOWStudy_PortalFrame_V02",
    "tornado_shell": ROOT + "/Meshes/SM_Revvy_CGHOWStudy_TornadoHelix_V03",
    "broken_ring": "/Game/VFX/Generated/Meshes/SM_RevvyVFX_BrokenRing",
    "sphere": "/Game/SoStylized/Demo/Meshes/SM_Sphere",
    "rock": "/Game/SoStylized/Effects/Meshes/SM_RockDebris01",
}

PATHS = {
    "cghow_study_impact": ROOT + "/Impact/NS_Revvy_CGHOWStudy_Impact_V01",
    "cghow_study_portal": ROOT + "/Portal/NS_Revvy_CGHOWStudy_Portal_V01",
    "cghow_study_lightning": ROOT + "/Lightning/NS_Revvy_CGHOWStudy_Lightning_V02",
    "cghow_study_water_splash": ROOT + "/Water/NS_Revvy_CGHOWStudy_WaterSplash_V01",
    "cghow_study_dark_tornado": ROOT + "/Tornado/NS_Revvy_CGHOWStudy_DarkTornado_V01",
}

MESH_RECIPES = {
    "portal_ring": {
        "path": MESHES["portal_ring"],
        "replace_existing": False,
        "parts": [
            {
                "type": "torus",
                "major_radius": 50.0,
                "minor_radius": 2.4,
                "major_steps": 72,
                "minor_steps": 8,
                "revolve_degrees": 248.0,
                "degree_offset": 18.0,
                "rotation": (90.0, 0.0, 0.0),
            },
            {
                "type": "torus",
                "major_radius": 44.0,
                "minor_radius": 1.25,
                "major_steps": 64,
                "minor_steps": 6,
                "revolve_degrees": 206.0,
                "degree_offset": 172.0,
                "rotation": (90.0, 0.0, 0.0),
            }
        ],
    },
    "tornado_shell": {
        "path": MESHES["tornado_shell"],
        "replace_existing": False,
        "parts": [
            {
                "type": "torus",
                "major_radius": 16.0 + index * 3.8,
                "minor_radius": 1.15 + index * 0.08,
                "major_steps": 32,
                "minor_steps": 6,
                "revolve_degrees": 145.0,
                "degree_offset": float((15 + index * 82) % 360),
                "location": (0.0, 0.0, float(index * 12)),
            }
            for index in range(12)
        ],
    },
}


CGHOW_TUTORIAL_PRESETS = {
    "cghow_study_impact": {
        "profile": "combat",
        "quality_gate": True,
        "quality_threshold": 84,
        "preview_range": (0.0, 1.4),
        "fixed_bounds": ((-450, -450, -450), (450, 450, 450)),
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "simulation_samples": (0.04, 0.09, 0.18, 0.42, 0.9),
        "style_brief": {
            "primary": "dark-core impact with an ivory-hot directional flash",
            "secondary": "staggered shock shell, broken arc, debris, and ember streaks",
            "palette": ["ivory", "amber", "deep crimson", "charcoal"],
            "timing": "0.00 core, 0.03 flash, 0.05 body, 0.09 ring, 0.18 decay",
            "cghow_principle": "DarkCore -> Flash -> Body -> Streaks -> Ring -> Decay",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "DarkCore",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime", "0.30"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(50,50)"],
                ],
                "tint": "(1,1,1)",
                "material": MATERIALS["dark_core"],
            },
            {
                "from": "EM_GlowCore",
                "as": "PrimaryFlash",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.03"],
                    ["InitializeParticle", "Lifetime", "0.24"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(180,30)"],
                ],
                "tint": "(3.2,1.35,0.24)",
                "material": MATERIALS["core"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurst",
                "fallback_from": "EM_MeshDebris",
                "as": "PrimaryBody",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "2"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.05"],
                    ["InitializeParticle", "Lifetime Min", "0.24"],
                    ["InitializeParticle", "Lifetime Max", "0.38"],
                    ["InitializeParticle", "Mesh Uniform Scale Min", "0.55"],
                    ["InitializeParticle", "Mesh Uniform Scale Max", "0.78"],
                    ["ShapeLocation", "Cylinder Radius", "18"],
                    ["ShapeLocation", "Cylinder Height", "5"],
                ],
                "tint": "(2.5,0.5,0.05)",
                "material": MATERIALS["mesh"],
                "mesh": MESHES["broken_ring"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "AccentStreaks",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "36"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.06"],
                    ["InitializeParticle", "Lifetime Min", "0.38"],
                    ["InitializeParticle", "Lifetime Max", "0.62"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(8,40)"],
                    ["ShapeLocation", "Cylinder Radius", "8"],
                    ["ShapeLocation", "Cylinder Height", "4"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "80"],
                    ["RandomRangeFloat001", "Minimum", "420"],
                    ["RandomRangeFloat001", "Maximum", "850"],
                    ["RandomRangeLinearColor", "Minimum", "(1.0,0.08,0.005,1)"],
                    ["RandomRangeLinearColor", "Maximum", "(1.0,0.72,0.12,1)"],
                    ["Drag", "Drag", "1.25"],
                ],
                "material": MATERIALS["streak"],
                "renderer_type": "Sprite",
                "renderer_properties": {"Alignment": "VelocityAligned"},
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricShockwave",
                "fallback_from": "EM_MeshDebris",
                "as": "SecondaryRing",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.09"],
                    ["InitializeParticle", "Lifetime", "0.38"],
                    ["InitializeParticle", "Lifetime Min", "0.36"],
                    ["InitializeParticle", "Lifetime Max", "0.4"],
                    ["InitializeParticle", "Mesh Uniform Scale", "0.68"],
                    ["InitializeParticle", "Color", "(0.65,0.08,0.01,0.16)"],
                    ["ShapeLocation", "Sphere Radius", "0"],
                ],
                "material": MATERIALS["ring"],
                "mesh": MESHES["broken_ring"],
            },
            {
                "from": "EM_ExpandingRing",
                "as": "BrokenFollowThrough",
                "mesh": MESHES["broken_ring"],
                "set": [
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.13"],
                    ["InitializeParticle", "Lifetime", "0.54"],
                    ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                    ["InitializeParticle", "Mesh Scale", "(1.02,1.02,0.06)"],
                    ["ScaleMeshSize", "Scale Factor", "(1,1,1)"],
                ],
                "tint": "(1.8,0.34,0.035)",
                "material": MATERIALS["ring"],
            },
            {
                "from": "EM_GroundDust",
                "as": "DecaySmoke",
                "remove_modules": ["ScaleSpriteSize"],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "16"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.14"],
                    ["InitializeParticle", "Lifetime", "1.05"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(42,42)"],
                    ["AddVelocityFromPoint", "Velocity Strength", "140"],
                    ["Drag", "Drag", "2.3"],
                ],
                "tint": "(0.24,0.11,0.05)",
                "material": MATERIALS["smoke"],
            },
        ],
    },
    "cghow_study_portal": {
        "profile": "ambient",
        "quality_gate": True,
        "quality_threshold": 82,
        "preview_range": (0.0, 2.8),
        "fixed_bounds": ((-450, -450, -150), (450, 450, 900)),
        "phases": ["anticipation", "emission", "body", "decay"],
        "simulation_samples": (0.05, 0.38, 0.75, 1.3, 2.6),
        "style_brief": {
            "primary": "vertical cyan-violet portal frame around a dark readable void",
            "secondary": "runic halo, rising mist, orbiting motes, and intermittent gate arcs",
            "palette": ["near-black", "cyan", "violet", "cold white"],
            "timing": "persistent core with offset 0.05-0.36 second secondary loops",
            "cghow_principle": "BlackPortal -> Glow -> Ring -> Smoke -> Aura -> Sparkles",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "BlackPortalCore",
                "remove_modules": ["SpawnRate", "ScaleSpriteSize"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["EmitterState", "Loop Duration", "8.0"],
                    ["InitializeParticle", "Lifetime", "8.0"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(152,152)"],
                ],
                "tint": "(0.015,0.008,0.028)",
                "material": MATERIALS["smoke"],
            },
            {
                "from": "EM_ExpandingRing",
                "as": "PortalFrame",
                "mesh": MESHES["portal_ring"],
                "set": [
                    ["EmitterState", "Loop Delay", "0.05"],
                    ["EmitterState", "Loop Duration", "8.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["InitializeParticle", "Lifetime", "8.0"],
                    ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                    ["InitializeParticle", "Mesh Scale", "(1.38,1.38,1.38)"],
                    ["ScaleMeshSize", "Scale Factor", "(1.02,1.02,1.02)"],
                ],
                "tint": "(0.08,1.2,1.8)",
                "material": MATERIALS["ring"],
            },
            {
                "from": "EM_GlowCore",
                "as": "RunicHalo",
                "remove_modules": ["SpawnRate", "ScaleSpriteSize"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "8.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.08"],
                    ["InitializeParticle", "Lifetime", "8.0"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(210,210)"],
                    ["InitializeParticle", "Position Offset", "(0,-4,0)"],
                ],
                "tint": "(0.35,0.85,2.4)",
                "material": MATERIALS["rune"],
            },
            {
                "from": "EM_RisingWisps",
                "as": "EnergyMist",
                "remove_modules": ["ScaleSpriteSize"],
                "add_modules": [["sphere_location", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "8.0"],
                    ["SpawnRate", "SpawnRate", "30"],
                    ["InitializeParticle", "Lifetime", "1.6"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(50,50)"],
                    ["InitializeParticle", "Position Offset", "(0,0,-12)"],
                    ["SphereLocation", "Sphere Radius", "65"],
                    ["AddVelocity", "Velocity", "(0,0,60)"],
                    ["CurlNoiseForce", "Noise Frequency", "12"],
                    ["CurlNoiseForce", "Noise Strength", "42"],
                    ["Drag", "Drag", "2.0"],
                ],
                "tint": "(0.08,0.55,1.25)",
                "material": MATERIALS["smoke"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "OrbitMotes",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "1.1"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "18"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.14"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["InitializeParticle", "Lifetime Min", "0.9"],
                    ["InitializeParticle", "Lifetime Max", "1.4"],
                    ["ShapeLocation", "Cylinder Radius", "72"],
                    ["ShapeLocation", "Cylinder Height", "110"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "82"],
                    ["RandomRangeFloat001", "Minimum", "45"],
                    ["RandomRangeFloat001", "Maximum", "120"],
                    ["RandomRangeLinearColor", "Minimum", "(0.02,0.2,0.8,0.7)"],
                    ["RandomRangeLinearColor", "Maximum", "(0.35,0.9,1.0,1)"],
                    ["Drag", "Drag", "2.2"],
                ],
                "material": MATERIALS["streak"],
                "renderer_type": "Sprite",
                "renderer_properties": {"Alignment": "VelocityAligned"},
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_LightningSingle",
                "fallback_from": "EM_ElectricArc",
                "as": "GateArcs",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "1.0"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.20"],
                    ["InitializeParticle", "Lifetime Min", "0.28"],
                    ["InitializeParticle", "Lifetime Max", "0.40"],
                    ["InitializeParticle", "Mesh Uniform Scale Min", "0.014"],
                    ["InitializeParticle", "Mesh Uniform Scale Max", "0.020"],
                    ["InitializeParticle", "Position Offset", "(58,0,42)"],
                ],
                "preserve_renderer": True,
                "renderer_type": "Mesh",
                "renderer_properties": {"Material": MATERIALS["lightning"]},
            },
        ],
    },
    "cghow_study_lightning": {
        "profile": "combat",
        "quality_gate": True,
        "quality_threshold": 84,
        "preview_range": (0.0, 1.5),
        "fixed_bounds": ((-650, -650, -650), (650, 650, 650)),
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "simulation_samples": (0.03, 0.14, 0.24, 0.45, 0.9),
        "style_brief": {
            "primary": "single readable lightning strike and compact electric body",
            "secondary": "charge rune, shock shell, ion particles, and residual haze",
            "palette": ["cold white", "electric cyan", "royal blue", "deep violet"],
            "timing": "0.00 charge, 0.12 strike, 0.15 body, 0.18 shell, 0.28 haze",
            "cghow_principle": "Charge -> HeroBolt -> Burst -> Shockwave -> Ions -> Residual",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "ChargeRune",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime", "0.18"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(76,76)"],
                ],
                "tint": "(0.08,0.5,2.2)",
                "material": MATERIALS["rune"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_LightningSingle",
                "fallback_from": "EM_ElectricArc",
                "as": "HeroLightning",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.12"],
                    ["InitializeParticle", "Lifetime Min", "0.34"],
                    ["InitializeParticle", "Lifetime Max", "0.40"],
                    ["InitializeParticle", "Mesh Uniform Scale Min", "0.04"],
                    ["InitializeParticle", "Mesh Uniform Scale Max", "0.05"],
                ],
                "preserve_renderer": True,
                "renderer_type": "Mesh",
                "renderer_properties": {"Material": MATERIALS["lightning"]},
            },
            {
                "from": "EM_GlowCore",
                "as": "PrimaryElectricBody",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.14"],
                    ["InitializeParticle", "Lifetime", "0.34"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(46,46)"],
                ],
                "tint": "(0.12,0.55,1.8)",
                "material": MATERIALS["core"],
            },
            {
                "from": "EM_ExpandingRing",
                "as": "ElectricShockwave",
                "mesh": MESHES["broken_ring"],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.17"],
                    ["InitializeParticle", "Lifetime", "0.48"],
                    ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                    ["InitializeParticle", "Mesh Scale", "(0.55,0.55,0.05)"],
                    ["ScaleMeshSize", "Scale Factor", "(1.2,1.2,1)"],
                ],
                "tint": "(0.08,0.5,1.8)",
                "material": MATERIALS["ring"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "ElectricIons",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "12"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.19"],
                    ["InitializeParticle", "Lifetime Min", "0.45"],
                    ["InitializeParticle", "Lifetime Max", "0.82"],
                    ["ShapeLocation", "Cylinder Radius", "10"],
                    ["ShapeLocation", "Cylinder Height", "5"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "68"],
                    ["RandomRangeFloat001", "Minimum", "280"],
                    ["RandomRangeFloat001", "Maximum", "700"],
                    ["RandomRangeLinearColor", "Minimum", "(0.02,0.15,0.8,0.8)"],
                    ["RandomRangeLinearColor", "Maximum", "(0.65,0.95,1.0,1)"],
                    ["Drag", "Drag", "1.7"],
                ],
                "material": MATERIALS["streak"],
                "renderer_type": "Sprite",
                "renderer_properties": {"Alignment": "VelocityAligned"},
            },
            {
                "from": "EM_GroundDust",
                "as": "ResidualHaze",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "9"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.26"],
                    ["InitializeParticle", "Lifetime", "1.05"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(42,42)"],
                    ["AddVelocityFromPoint", "Velocity Strength", "95"],
                    ["Drag", "Drag", "2.7"],
                ],
                "tint": "(0.025,0.12,0.55)",
                "material": MATERIALS["smoke"],
            },
        ],
    },
    "cghow_study_water_splash": {
        "profile": "combat",
        "quality_gate": True,
        "quality_threshold": 82,
        "preview_range": (0.0, 1.3),
        "fixed_bounds": ((-550, -550, -550), (550, 550, 550)),
        "phases": ["anticipation", "emission", "body", "impact", "decay"],
        "simulation_samples": (0.03, 0.07, 0.12, 0.18, 0.45, 0.9),
        "style_brief": {
            "primary": "white-cyan crown splash with readable ballistic droplets",
            "secondary": "mist sheet, delayed surface ripple, rain crown, and a short specular flash",
            "palette": ["white", "cyan", "aqua", "transparent blue"],
            "timing": "0.00 flash, 0.03 splash, 0.06 mist, 0.10 ripple, 0.14 crown",
            "cghow_principle": "SurfaceFlash -> SplashMesh -> Droplets -> Mist -> DelayedRipple",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "SurfaceFlash",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.0"],
                    ["InitializeParticle", "Lifetime", "0.24"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(70,70)"],
                ],
                "tint": "(0.35,1.3,2.2)",
                "material": MATERIALS["core"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_WaterSplashParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "SplashBody",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "44"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.03"],
                    ["InitializeParticle", "Color", "(0.12,0.62,1.0,0.92)"],
                    ["InitializeParticle", "Uniform Sprite Size Min", "14"],
                    ["InitializeParticle", "Uniform Sprite Size Max", "38"],
                    ["ShapeLocation", "Box Size", "(60,60,20)"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "44"],
                    ["AddVelocity", "Velocity", "(0,0,420)"],
                ],
                "preserve_renderer": True,
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_WaterSplashMist",
                "fallback_from": "EM_GroundDust",
                "as": "SplashMist",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "10"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.06"],
                    ["InitializeParticle", "Color", "(0.18,0.7,1.0,0.32)"],
                    ["InitializeParticle", "Uniform Sprite Size Min", "45"],
                    ["InitializeParticle", "Uniform Sprite Size Max", "90"],
                    ["ShapeLocation", "Box Size", "(70,70,25)"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "68"],
                    ["AddVelocity", "Velocity", "(0,0,170)"],
                ],
                "preserve_renderer": True,
            },
            {
                "from": "EM_ExpandingRing",
                "as": "SurfaceRipple",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.10"],
                    ["InitializeParticle", "Lifetime", "0.8"],
                    ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                    ["InitializeParticle", "Mesh Scale", "(0.62,0.62,0.05)"],
                    ["InitializeParticle", "Position Offset", "(0,0,3)"],
                    ["ScaleMeshSize", "Scale Factor", "(1.8,1.8,1)"],
                ],
                "tint": "(0.08,0.75,1.6)",
                "material": MATERIALS["ring"],
                "mesh": MESHES["broken_ring"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_RainSplash",
                "fallback_from": "EM_MeshDebris",
                "as": "RainCrown",
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "2"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.14"],
                    ["InitializeParticle", "Lifetime", "0.42"],
                    ["InitializeParticle", "Mesh Uniform Scale", "3.4"],
                    ["InitializeParticle", "Position Offset", "(0,0,8)"],
                ],
                "material": "/Game/SoStylized/Effects/Materials/MI_RainSplash",
                "mesh": "/Game/SoStylized/Effects/Meshes/SM_RainSplash",
            },
        ],
    },
    "cghow_study_dark_tornado": {
        "profile": "ambient",
        "quality_gate": True,
        "quality_threshold": 82,
        "preview_range": (0.0, 3.0),
        "fixed_bounds": ((-500, -500, -500), (500, 500, 500)),
        "phases": ["anticipation", "emission", "body", "decay"],
        "simulation_samples": (0.05, 0.44, 0.9, 1.6, 2.9),
        "style_brief": {
            "primary": "tapered helical smoke funnel with a dense shadow core",
            "secondary": "faint broken guide bands, intermittent arcs, and sparse storm motes",
            "palette": ["near-black", "muted violet", "magenta", "cold white"],
            "timing": "persistent body with 0.06-0.42 second offset loops",
            "cghow_principle": "TornadoMesh -> Smoke -> Sparks -> Lightning -> SwirlRings",
        },
        "blocks": [
            {
                "from": "EM_GlowCore",
                "as": "ShadowCore",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "10.0"],
                    ["SpawnRate", "SpawnRate", "2"],
                    ["InitializeParticle", "Lifetime", "1.4"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(112,112)"],
                ],
                "tint": "(0.06,0.01,0.10)",
                "material": MATERIALS["smoke"],
            },
            {
                "source_system": "/Game/VFX/NS_SmokeAura",
                "source_emitter": "Fountain",
                "fallback_from": "EM_RisingWisps",
                "as": "TornadoBody",
                "remove_modules": ["SkeletalMeshLocation", "SolveForcesAndVelocity"],
                "add_modules": [["vortex_force", "Update"], ["solve_forces", "Update"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.06"],
                    ["EmitterState", "Loop Duration", "10.0"],
                    ["SpawnRate", "SpawnRate", "40"],
                    ["InitializeParticle", "Lifetime Min", "1.5"],
                    ["InitializeParticle", "Lifetime Max", "2.4"],
                    ["InitializeParticle", "Uniform Sprite Size Min", "120"],
                    ["InitializeParticle", "Uniform Sprite Size Max", "180"],
                    ["InitializeParticle", "Position Offset", "(0,0,0)"],
                    ["RandomRangeVector", "Minimum", "(-24,-24,25)"],
                    ["RandomRangeVector", "Maximum", "(24,24,90)"],
                    ["VortexForce", "Vortex Axis", "(0,0,1)"],
                    ["VortexForce", "Vortex Force Amount", "240"],
                    ["VortexForce", "Origin Pull Amount", "85"],
                    ["VortexForce", "Influence Falloff Radius", "190"],
                ],
                "tint": "(1.1,0.18,1.8)",
                "material": MATERIALS["tornado_smoke"],
            },
            {
                "from": "EM_ExpandingRing",
                "as": "FunnelShell",
                "mesh": MESHES["tornado_shell"],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "3.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "1"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.12"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["InitializeParticle", "Lifetime", "3.2"],
                    ["InitializeParticle", "Mesh Scale Mode", "Non-Uniform"],
                    ["InitializeParticle", "Mesh Scale", "(0.88,0.88,0.92)"],
                    ["ScaleMeshSize", "Scale Factor", "(1.04,1.04,1.08)"],
                ],
                "tint": "(0.65,0.12,1.20)",
                "material": MATERIALS["tornado_guide"],
            },
            {
                "from": "EM_RisingWisps",
                "as": "InnerWisps",
                "remove_modules": ["SolveForcesAndVelocity"],
                "add_modules": [["sphere_location", "Spawn"], ["vortex_force", "Update"], ["solve_forces", "Update"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.18"],
                    ["EmitterState", "Loop Duration", "10.0"],
                    ["SpawnRate", "SpawnRate", "48"],
                    ["InitializeParticle", "Lifetime", "1.8"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(60,60)"],
                    ["InitializeParticle", "Position Offset", "(0,0,20)"],
                    ["AddVelocity", "Velocity", "(0,0,140)"],
                    ["CurlNoiseForce", "Noise Frequency", "18"],
                    ["CurlNoiseForce", "Noise Strength", "65"],
                    ["Drag", "Drag", "1.2"],
                    ["SphereLocation", "Sphere Radius", "34"],
                    ["VortexForce", "Vortex Axis", "(0,0,1)"],
                    ["VortexForce", "Vortex Force Amount", "340"],
                    ["VortexForce", "Origin Pull Amount", "120"],
                    ["VortexForce", "Influence Falloff Radius", "180"],
                ],
                "tint": "(0.72,0.12,1.45)",
                "material": MATERIALS["tornado_smoke"],
            },
            {
                "from": "EM_RisingWisps",
                "as": "OuterWisps",
                "remove_modules": ["SolveForcesAndVelocity"],
                "add_modules": [["sphere_location", "Spawn"], ["vortex_force", "Update"], ["solve_forces", "Update"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "10.0"],
                    ["SpawnRate", "SpawnRate", "32"],
                    ["InitializeParticle", "Lifetime", "2.2"],
                    ["InitializeParticle", "Sprite Size Mode", "Non-Uniform"],
                    ["InitializeParticle", "Sprite Size", "(90,90)"],
                    ["InitializeParticle", "Position Offset", "(0,0,45)"],
                    ["AddVelocity", "Velocity", "(0,0,165)"],
                    ["CurlNoiseForce", "Noise Frequency", "10"],
                    ["CurlNoiseForce", "Noise Strength", "90"],
                    ["Drag", "Drag", "1.0"],
                    ["SphereLocation", "Sphere Radius", "68"],
                    ["VortexForce", "Vortex Axis", "(0,0,1)"],
                    ["VortexForce", "Vortex Force Amount", "260"],
                    ["VortexForce", "Origin Pull Amount", "72"],
                    ["VortexForce", "Influence Falloff Radius", "260"],
                ],
                "tint": "(0.95,0.16,1.85)",
                "material": MATERIALS["tornado_smoke"],
            },
            {
                "from": "EM_ElectricArc",
                "as": "StormArcs",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "1.2"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "3"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.34"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["InitializeParticle", "Lifetime", "0.7"],
                    ["InitializeRibbon", "Lifetime", "0.7"],
                    ["InitializeRibbon", "Ribbon Width", "6"],
                    ["AddVelocityFromPoint", "Velocity Strength", "220"],
                ],
                "tint": "(0.7,0.2,1.8)",
                "material": MATERIALS["streak"],
            },
            {
                "from": "EM_ElectricArc",
                "as": "StormArcsLate",
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "3.0"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "2"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "2.0"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1"],
                    ["InitializeParticle", "Lifetime", "1.1"],
                    ["InitializeRibbon", "Lifetime", "1.1"],
                    ["InitializeRibbon", "Ribbon Width", "4"],
                    ["AddVelocityFromPoint", "Velocity Strength", "180"],
                ],
                "tint": "(0.5,0.12,1.4)",
                "material": MATERIALS["streak"],
            },
            {
                "asset": "/Game/SoStylized/Effects/Emitters/NE_ElectricBurstParticles",
                "fallback_from": "EM_BurstSparks",
                "as": "StormMotes",
                "remove_modules": ["SpawnRate"],
                "add_modules": [["burst", "Spawn"]],
                "set": [
                    ["EmitterState", "Loop Delay", "0.0"],
                    ["EmitterState", "Loop Duration", "0.75"],
                    ["SpawnBurst_Instantaneous", "SpawnCount", "10"],
                    ["SpawnBurst_Instantaneous", "Spawn Time", "0.42"],
                    ["SpawnBurst_Instantaneous", "Loop Count Limit", "1000"],
                    ["InitializeParticle", "Lifetime Min", "0.75"],
                    ["InitializeParticle", "Lifetime Max", "1.05"],
                    ["ShapeLocation", "Cylinder Radius", "34"],
                    ["ShapeLocation", "Cylinder Height", "100"],
                    ["AddVelocity", "Cone Axis", "(0,0,1)"],
                    ["AddVelocity", "Cone Angle", "72"],
                    ["RandomRangeFloat001", "Minimum", "45"],
                    ["RandomRangeFloat001", "Maximum", "110"],
                    ["RandomRangeLinearColor", "Minimum", "(0.12,0.01,0.38,0.7)"],
                    ["RandomRangeLinearColor", "Maximum", "(0.8,0.18,1.0,1)"],
                    ["Drag", "Drag", "2.0"],
                ],
                "material": MATERIALS["streak"],
                "renderer_type": "Sprite",
                "renderer_properties": {"Alignment": "VelocityAligned"},
            },
        ],
    },
}


# Apply the versioned detail pass after retaining the original study recipes
# above as readable provenance. Existing V01/V02 assets are never replaced.
from revvy_cghow_detail_v2 import (  # noqa: E402
    POLISH_V2_MATERIALS,
    POLISH_V2_MATERIAL_RECIPES,
    POLISH_V2_MESHES,
    POLISH_V2_MESH_RECIPES,
    POLISH_V2_PATHS,
    POLISH_V2_PRESET_OVERRIDES,
)

MATERIALS.update(POLISH_V2_MATERIALS)
MATERIAL_RECIPES.update(POLISH_V2_MATERIAL_RECIPES)
MESHES.update(POLISH_V2_MESHES)
MESH_RECIPES.update(POLISH_V2_MESH_RECIPES)
PATHS.update(POLISH_V2_PATHS)
for _preset_name, _overrides in POLISH_V2_PRESET_OVERRIDES.items():
    CGHOW_TUTORIAL_PRESETS[_preset_name].update(deepcopy(_overrides))


CGHOW_OVERRIDE_KEYS = {
    "quality_threshold",
    "simulation_samples",
    "warnings_as_errors",
}


def validate_cghow_presets(presets=None):
    """Validate collection-level contracts without importing Unreal Python."""
    collection = presets or CGHOW_TUTORIAL_PRESETS
    errors = []
    if set(collection) != set(PATHS):
        errors.append("preset keys must exactly match PATHS")

    output_paths = []
    for preset_name, spec in collection.items():
        path = PATHS.get(preset_name, "")
        output_paths.append(path)
        if not path.startswith(ROOT + "/") or "/NS_Revvy_CGHOWStudy_" not in path:
            errors.append(f"{preset_name}: invalid study output path")

        blocks = list(spec.get("blocks") or [])
        if not 3 <= len(blocks) <= 12:
            errors.append(f"{preset_name}: expected 3-12 blocks")
        aliases = [str(block.get("as") or "") for block in blocks]
        if any(not alias for alias in aliases) or len(aliases) != len(set(aliases)):
            errors.append(f"{preset_name}: emitter aliases must be non-empty and unique")

        spawn_times = set()
        for block in blocks:
            source_count = sum(
                bool(block.get(key))
                for key in ("from", "asset", "source_emitter")
            )
            if source_count != 1:
                errors.append(f"{preset_name}/{block.get('as')}: exactly one source is required")
            if block.get("asset") and not block.get("fallback_from"):
                errors.append(f"{preset_name}/{block.get('as')}: asset source requires fallback_from")
            if bool(block.get("source_system")) != bool(block.get("source_emitter")):
                errors.append(f"{preset_name}/{block.get('as')}: source system/emitter must be paired")

            delay = 0.0
            burst_time = 0.0
            for module_name, input_name, value in block.get("set", []):
                normalized_module = str(module_name).replace(" ", "").casefold()
                normalized_input = str(input_name).replace(" ", "").casefold()
                try:
                    numeric_value = float(value)
                except (TypeError, ValueError):
                    continue
                if normalized_module == "emitterstate" and normalized_input == "loopdelay":
                    delay = numeric_value
                elif normalized_module.startswith("spawnburst") and normalized_input == "spawntime":
                    burst_time = numeric_value
                elif (
                    spec.get("profile") in ("combat", "cinematic")
                    and normalized_module == "spawnrate"
                    and normalized_input == "spawnrate"
                    and numeric_value > 0
                ):
                    errors.append(f"{preset_name}/{block.get('as')}: one-shot preset cannot use SpawnRate")
            spawn_times.add(round(delay + burst_time, 3))
        if len(blocks) >= 3 and len(spawn_times) < 3:
            errors.append(f"{preset_name}: at least three staggered spawn times are required")

    if len(output_paths) != len(set(output_paths)):
        errors.append("study output paths must be unique")
    return {"success": not errors, "preset_count": len(collection), "errors": errors}


def get_preset(name):
    return deepcopy(CGHOW_TUTORIAL_PRESETS[name])
