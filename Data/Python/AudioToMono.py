import unreal, subprocess, os

AUDIO_DIR = "/Game/Audio/SoundBase/3D"
TEMP = "D:/temp_audio"

os.makedirs(TEMP, exist_ok=True)
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

for path in lib.list_assets(AUDIO_DIR, recursive=True):
    asset = lib.load_asset(path)
    if not isinstance(asset, unreal.SoundWave):
        continue
    if asset.get_editor_property("num_channels") == 1:
        unreal.log(f"건너뜀 (이미 모노): {path}")
        continue

    name = path.split("/")[-1].split(".")[0]
    folder = path.rsplit("/", 1)[0]
    src = os.path.join(TEMP, f"{name}.wav")
    dst = os.path.join(TEMP, f"{name}_mono.wav")

    # Export
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = src
    task.automated = True
    task.prompt = False
    task.exporter = unreal.SoundExporterWAV()
    unreal.Exporter.run_asset_export_task(task)

    # 모노 변환
    subprocess.run(["ffmpeg", "-y", "-i", src, "-ac", "1", dst], check=True)

    # 덮어쓰기
    imp = unreal.AssetImportTask()
    imp.filename = dst
    imp.destination_path = folder
    imp.destination_name = name
    imp.replace_existing = True
    imp.automated = True
    imp.save = True
    tools.import_asset_tasks([imp])

    unreal.log(f"완료: {name}")