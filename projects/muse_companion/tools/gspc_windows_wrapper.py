"""Run the pinned GSPC compiler and keep its depfile Ninja-compatible on Windows."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def _find_gspc() -> Path:
    local_app_data = Path(os.environ.get("LOCALAPPDATA", ""))
    candidates = []
    if local_app_data:
        candidates.extend(
            (local_app_data / "esp-mosaico" / "gspc").glob("*/gspc.exe")
        )
    user_profile = Path(os.environ.get("USERPROFILE", ""))
    if user_profile:
        candidates.extend(
            (user_profile / ".cache" / "esp-mosaico" / "gspc").glob(
                "*/gspc.exe"
            )
        )
    explicit = os.environ.get("GSPC_REAL_EXECUTABLE")
    if explicit:
        candidates.insert(0, Path(explicit))
    candidates = [path for path in candidates if path.is_file()]
    if not candidates:
        raise SystemExit("GSPC compiler executable was not found")
    pinned=[path for path in candidates if path.parent.name=="0.6.1"]
    if not pinned: raise SystemExit("Pinned GSPC 0.6.1 not found")
    return pinned[0]


def _depfile_argument(args: list[str]) -> Path | None:
    try:
        index = args.index("--depfile")
    except ValueError:
        return None
    if index + 1 >= len(args):
        return None
    return Path(args[index + 1])


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] in ('compile','pack','build') and '--profile' not in args:
        # The public ui_apps helper derives a four-player default profile.
        # This app declares eight authored states in its own compiler profile.
        scenes=[Path(a) for a in args[1:] if a.endswith('.json') and Path(a).is_file()]
        if scenes:
            profile=scenes[0].resolve().parent/'companion_rgb565.yaml'
            if profile.is_file():
                for option in ('--platform-soc','--pixel-format'):
                    if option in args:
                        at=args.index(option);del args[at:at+2]
                args=[a for a in args if a not in ('--psram','--hardware-jpeg','--software-jpeg','--ppa','--dma2d')]
                args.extend(['--profile',str(profile)])
    result = subprocess.run([str(_find_gspc()), *args], check=False)
    if result.returncode != 0:
        return result.returncode

    depfile = _depfile_argument(args)
    if depfile is not None and depfile.exists():
        # GSPC emits valid escaped POSIX paths, but CMake 4's Windows Ninja
        # depfile transformer turns drive-letter dependencies into /D:/...
        # paths. The scene JSON is already a direct CMake dependency; keeping
        # this depfile target-only preserves reliable builds without that bug.
        depfile.write_text(f"{depfile.stem}:\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
