from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import sys
import zipfile


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
PACKAGE_DIR = SCRIPT_DIR / "revvy_blender_bridge"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from revvy_blender_bridge.common import BridgeError, ensure_config  # noqa: E402


def build_addon_zip(destination: pathlib.Path, *, overwrite: bool) -> pathlib.Path:
    target = destination.expanduser().resolve()
    if target.suffix.lower() != ".zip":
        raise BridgeError("invalid_arguments", "Add-on archive must use the .zip extension.")
    if target.exists() and not overwrite:
        raise BridgeError("file_already_exists", f"Archive already exists: {target}")
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(f".{target.name}.tmp")
    with zipfile.ZipFile(
        temporary,
        mode="w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for source in sorted(PACKAGE_DIR.rglob("*")):
            if source.is_file() and "__pycache__" not in source.parts:
                archive.write(
                    source,
                    pathlib.PurePosixPath(PACKAGE_DIR.name)
                    / source.relative_to(PACKAGE_DIR),
                )
    temporary.replace(target)
    return target


def install_to_addons(
    addons_dir: pathlib.Path,
    *,
    overwrite: bool,
) -> pathlib.Path:
    root = addons_dir.expanduser().resolve()
    if not root.is_dir():
        raise BridgeError(
            "directory_not_found",
            f"Explicit Blender add-ons directory does not exist: {root}",
        )
    destination = root / PACKAGE_DIR.name
    if destination.exists():
        if not overwrite:
            raise BridgeError(
                "addon_already_exists",
                f"Add-on already exists: {destination}. Use --overwrite explicitly.",
            )
        shutil.rmtree(destination)
    shutil.copytree(
        PACKAGE_DIR,
        destination,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
    )
    return destination


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Package or install the Revvy Blender Bridge. No Blender location is guessed; "
            "pass --addons-dir for direct installation."
        )
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--zip",
        type=pathlib.Path,
        help="Write a legacy Blender add-on zip to this exact path.",
    )
    mode.add_argument(
        "--addons-dir",
        type=pathlib.Path,
        help="Copy the package into this exact existing Blender scripts/addons directory.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Explicitly replace an existing archive or installed Revvy add-on.",
    )
    options = parser.parse_args(argv)

    try:
        config = ensure_config()
        if options.addons_dir:
            installed = install_to_addons(
                options.addons_dir,
                overwrite=options.overwrite,
            )
            result = {
                "success": True,
                "installed_addon": str(installed),
                "config": config.public_dict(),
                "next_step": (
                    "Enable 'Revvy Blender Bridge' in Blender Preferences > Add-ons."
                ),
            }
        else:
            target = options.zip or (
                config.exchange_root / "revvy_blender_bridge.zip"
            )
            archive = build_addon_zip(target, overwrite=options.overwrite)
            result = {
                "success": True,
                "addon_zip": str(archive),
                "config": config.public_dict(),
                "next_step": (
                    "In Blender Preferences, choose Install from Disk, select this zip, "
                    "then enable 'Revvy Blender Bridge'."
                ),
            }
    except BridgeError as exc:
        print(
            json.dumps(
                {"success": False, "error": exc.as_dict()},
                ensure_ascii=True,
                indent=2,
            )
        )
        return 1

    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
