# Build and preview

Use Python 3.10+, ESP32-S31 (`esp32s31`), and ESP-IDF commit `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`. Do not substitute the latest IDF master. Initialize the pinned dependencies first:

```powershell
python tools/bootstrap.py
python -m pip install -r requirements.txt
```

Set `IDF_PATH` to your pinned checkout, verify with `git -C <idf-path> rev-parse HEAD`, then run the dependency-owned build runner from repository root:

```powershell
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/muse_companion doctor
python projects/muse_companion/tools/prepare_ui.py --character original
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/muse_companion build
```

This 2.0 candidate pins GSP 1.5.1 and gspc 0.6.1; component resolution needs network access. On Windows, use the configured IDF tools environment. The retained `vibe_mode` test partition is smaller than the application: the application installs into `main_app` (OTA 0). Do not resize Vibe Mode to silence a warning. See [migration status](V2_MIGRATION_CN.md) before installing anything.

Preview after component resolution:

```powershell
$env:GSP_SIM_DYNAMIC_FONT=(Resolve-Path projects/muse_companion/ui/fonts/NotoSansSC-Regular.ttf).Path
python mosaico.py project sim --project projects/muse_companion --interactive
```

Unit checks (run the produced setup_tests executable after building):

```powershell
cmake -S projects/muse_companion/tools/tests -B .codex-runs/setup-tests
cmake --build .codex-runs/setup-tests --config Release
```

From the IDF tools environment, build the full resource bundle with `cmake --build projects/muse_companion/build --target system-update-bundle`. Use `iris system-update` when resources or layout change; use `iris app-update` only for code changes with the identical full partition table. See dependency documentation under `submodule/esp-mosaico-utils/mosaico-tools/docs/` for application integration and device operations.
