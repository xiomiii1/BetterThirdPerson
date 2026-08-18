# BetterThirdPerson

BetterThirdPerson is a native Minecraft Bedrock Android mod for
LeviLaunchroid/Preloader.

**Author:** xiomi

**Version:** 1.0.1

## Features

- Independent third-person camera rotation
- 360-degree camera control
- Camera-relative movement
- 8-way movement
- Keeps player body rotation independent from camera rotation
- Third-person perspective support
- Local-player detection
- Native signature resolution
- Runtime hook installation
- ARM64 Android build
- Automatic `.levipack` packaging

## Architecture

BetterThirdPerson does not include BedrockTools.

The project contains its own:

- Signature definitions
- Signature resolution
- Hook management
- Camera state
- Movement transformation
- Preloader module registration
- `.levipack` packaging

## Build

Install:

- Android NDK
- xmake
- Python 3
- Preloader Android development dependencies

Configure:

```bash
xmake f -y -p android -a arm64-v8a -m release
