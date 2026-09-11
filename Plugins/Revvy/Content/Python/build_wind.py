# build_wind.py — NS_Wind 1회 빌드 (levy_vfx spec)
# 4레이어 바람: 컬노이즈 공기흐름 + 드리프트 먼지 + 나뭇잎 데브리 + 돌풍 streak.
# 톤: 호러(Oblivio) 기본 — 채도 낮은 쿨그레이, 낮은 emissive(soft translucent). 모션은 명확.
import unreal, sys
sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
import importlib, levy_vfx
importlib.reload(levy_vfx)

MI = "/Revvy/VFX/Materials"

spec = {
    "path": "/Game/VFX/NS_Wind",
    "blocks": [
        # 1) 주력 연속 공기흐름 — RisingWisps 내장 컬노이즈가 소용돌이 드리프트 제공
        {"from": "EM_RisingWisps", "as": "AirCurrent",
         "set": [["SpawnRate", "SpawnRate", "55"],
                 ["InitializeParticle", "Lifetime", "2.5"]],
         "tint": "(0.5, 0.52, 0.55)", "material": MI + "/MI_LevySmoke"},
        # 2) 바닥 먼지 드리프트
        {"from": "EM_GroundDust", "as": "DustDrift",
         "set": [["SpawnBurst_Instantaneous", "SpawnCount", "50"],
                 ["InitializeParticle", "Lifetime", "2.2"]],
         "tint": "(0.46, 0.44, 0.40)", "material": MI + "/MI_LevySmoke"},
        # 3) 휩쓸리는 나뭇잎/파편
        {"from": "EM_MeshDebris", "as": "Leaves",
         "set": [["InitializeParticle", "Lifetime", "3.0"]],
         "tint": "(0.30, 0.28, 0.22)"},
        # 4) 희미한 돌풍 라인
        {"from": "EM_RibbonTrail", "as": "GustStreak",
         "set": [["SpawnRate", "SpawnRate", "28"],
                 ["InitializeParticle", "Lifetime", "0.7"]],
         "tint": "(0.60, 0.62, 0.66)"},
    ],
}

print(levy_vfx.build(spec))
