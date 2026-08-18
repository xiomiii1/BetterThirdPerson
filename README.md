# BetterThirdPerson

Native Minecraft Bedrock Android client mod for LeviLaunchroid/Preloader.

## What it does
- Independent third-person camera yaw/pitch.
- Camera-relative 8-way movement without continuously snapping body rotation to the camera.
- Preserves normal third-person perspective, rendering, and interaction paths instead of replacing the renderer.
- Uses the game's `MoveInputComponent` camera-orientation path, which is also used by Bedrock's modern camera/control systems.

## Important compatibility note
This is a Bedrock Android native module. Forge, Fabric, and NeoForge are Java Edition loaders and are not supported by a `.levipack`/Preloader native module.

## Build
Use GitHub Actions or an Android xmake environment with Preloader Android and EnTT available.
