# Muse Charm · Mosaico

> **2.0 migration candidate**: the application and companion Vibe Mode build and
> pass the official 0.2 bundle checks. The original character and gesture UI pass
> the new native simulator checks. Device acceptance is still pending.
> See [migration notes](docs/V2_MIGRATION_CN.md). Do not use the legacy firmware
> or Recovery installation sequence on this branch. The verified first release
> remains on [main](https://github.com/samyeei/Muse-charm-mosaico/tree/main).

A voice AI companion for ESP-Mosaico, powered by Muse Gadget SDK, with push-to-talk conversation, Qwen speech, eight animated character states, Chinese/English UI and phone Wi-Fi setup. Generate and replace characters through Muse.

[中文](README.md) · [Illustrated setup guide (Chinese)](docs/SETUP_CN.md) · [Build](docs/BUILDING.md)

## Device demo

[![Watch the device demo](docs/assets/setup/12-muse-online.png)](https://github.com/samyeei/Muse-charm-mosaico/raw/refs/heads/main/docs/assets/demo/muse-charm-demo.mp4)

[Watch / download the demo](https://github.com/samyeei/Muse-charm-mosaico/raw/refs/heads/main/docs/assets/demo/muse-charm-demo.mp4): about 39 seconds, with audio. Shows conversation, character motion and settings. Recording location metadata was removed and the background header was blurred.

Features: a black background and original cream plush character with eight animated states; Qwen3-TTS-Flash-Realtime / Cherry; optional SiliconFlow and OpenAI-compatible PCM services; Chinese/English settings; live volume and brightness sliders; adjustable speech rate, auto pagination and screen timeout; touch and motion wake-up; phone provisioning over a password-free device hotspot; Muse-generated character previews and resource updates.

Bring your own Muse SDK token and TTS API key. The source and included firmware contain no personal credentials. Provision through the device hotspot at http://192.168.4.1; saving closes the hotspot and restarts the device. Use the Muse app for Wi-Fi pairing.

## Ask a local agent to install

Connect your ESP-Mosaico with a USB data cable, then send this prompt to Codex or another local agent with access to your computer and USB device:

> Check the release status of https://github.com/samyeei/Muse-charm-mosaico first. For a migration candidate, only initialize dependencies, build and preview in the simulator. Install only a device-validated stable release, using mosaico.py and its documented migration procedure; preserve device identity, credentials and NAND data, avoid whole-flash erasure, and verify startup, pairing, text replies and speech.

The agent needs local file, network and device access. It may request permissions or ask you to enter recovery mode physically. After installation, enter your own credentials on the phone configuration page; see the [illustrated guide (Chinese)](docs/SETUP_CN.md).

## Manual installation

Install Python 3.10+ and Git, run `python tools/bootstrap.py`, then read the [2.0 migration notes](docs/V2_MIGRATION_CN.md) and [build guide](docs/BUILDING.md). This candidate requires Vibe Mode 0.2 / ABI 2; legacy installation commands do not apply. The unvalidated 2.0 application candidate is in `firmware/candidates/`; keep using main for the verified first release. Device operations use `mosaico.py`; do not erase the whole flash. Video and accessory light-sensor integration are not implemented. The simulator mocks cloud/audio services.

Application code: Apache-2.0. Bundled SDK and fonts retain their licenses; see NOTICE. Dependency revisions are pinned in DEPENDENCIES.json. The internal firmware project remains `muse_companion`.
