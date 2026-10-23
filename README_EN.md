# Muse Charm · Mosaico

> **Muse Charm 2.0** uses Vibe Mode 0.2 / ABI 2. The original character, voice chat,
> Qwen playback and gesture UI passed user acceptance. Character installation and
> restore on ABI 2 still await validation. Read the [migration notes](docs/V2_MIGRATION_CN.md).
> [Download firmware](firmware/v2.0.0/muse-charm-v2.0.0.irisfw) ·
> [Retained first-generation source](https://github.com/samyeei/Muse-charm-mosaico/tree/8d94c42).

A voice AI companion for ESP-Mosaico, powered by Muse Gadget SDK, with push-to-talk conversation, Qwen speech, eight animated character states, Chinese/English UI and phone Wi-Fi setup. Generate and replace characters through Muse.

[中文](README.md) · [Illustrated setup guide (Chinese)](docs/SETUP_CN.md) · [Build](docs/BUILDING.md)

## Device demo

[![Watch the device demo](docs/assets/setup/12-muse-online.png)](https://github.com/samyeei/Muse-charm-mosaico/raw/refs/heads/main/docs/assets/demo/muse-charm-demo.mp4)

[Watch / download the demo](https://github.com/samyeei/Muse-charm-mosaico/raw/refs/heads/main/docs/assets/demo/muse-charm-demo.mp4): about 39 seconds, with audio. Shows conversation, character motion and settings. Recording location metadata was removed and the background header was blurred.

Features: a black background and original cream plush character with eight animated states; Qwen3-TTS-Flash-Realtime / Cherry; optional SiliconFlow and OpenAI-compatible PCM services; Chinese/English settings; live volume and brightness sliders; adjustable speech rate, auto pagination and screen timeout; touch and motion wake-up; phone provisioning over a password-free device hotspot; Muse-generated character previews and resource updates.

Bring your own Muse SDK token and TTS API key. The source and included firmware contain no personal credentials. Provision through the device hotspot at http://192.168.4.1; saving closes the hotspot and restarts the device. Use the Muse app for Wi-Fi pairing.

## Ask a local agent to install

Connect your ESP-Mosaico with a USB data cable, then send this prompt to Codex or another local agent with access to your computer and USB device:

> Install version 2.0 from https://github.com/samyeei/Muse-charm-mosaico on my USB Mosaico. Read the ABI 2 migration notes first, identify the current ABI, install this repository's custom Vibe Mode 0.2 when required, then install the full bundle in firmware/v2.0.0/. Use mosaico.py; preserve identity, credentials and NAND data without whole-flash erasure. Voice, speech and UI passed device acceptance; ABI 2 character transactions remain under validation. Verify the UI, text replies and speech after installation.

The agent needs local file, network and device access. It may request permissions or ask you to enter recovery mode physically. After installation, enter your own credentials on the phone configuration page; see the [illustrated guide (Chinese)](docs/SETUP_CN.md).

## Manual installation

Install Python 3.10+ and Git, run `python tools/bootstrap.py`, then read the [2.0 migration notes](docs/V2_MIGRATION_CN.md) and [build guide](docs/BUILDING.md). This candidate requires Vibe Mode 0.2 / ABI 2; legacy installation commands do not apply. The published 2.0 firmware is in `firmware/v2.0.0/`. Voice and gestures passed user acceptance; ABI 2 character transactions still await validation. Device operations use `mosaico.py`; do not erase the whole flash. Video and accessory light-sensor integration are not implemented. The simulator mocks cloud/audio services.

Application code: Apache-2.0. Bundled SDK and fonts retain their licenses; see NOTICE. Dependency revisions are pinned in DEPENDENCIES.json. The internal firmware project remains `muse_companion`.
