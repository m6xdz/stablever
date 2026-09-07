# OSU!BAND port notes

## Runtime

The known-working osu!lazer reader from the 2026.804.2 clean runtime port is retained. Gameplay module source files were not edited.

## Lazer-only cleanup

Removed:
- `core/game/osu_stable.hxx`
- `impl/defs/offsets_stable.hxx`
- stable attach branch from `client_factory.hxx`
- stable parser ownership from the cache
- stable-only songs path override UI

The generic `.osu` text parser used by lazer's file-store loader was renamed from `stable_parser.hxx` to `osu_file_parser.hxx`; its parsing logic was preserved.

## Interface

The new menu is inspired by the information architecture of modern sidebar cheat UIs, but uses an original OSU!BAND visual language:
- midnight/navy glass panels
- cyan + osu-inspired pink dual accents
- animated top accent rail
- animated waveform details
- grouped GAMEPLAY / UTILITY navigation
- animated vector OSU!BAND pulse logo
- live LAZER READY state pill
- safe profile slot in the lower-left

Existing control values and module wiring remain unchanged.

## Loader

The optional loader:
- detects `osu!.exe`
- can start the default `%LOCALAPPDATA%\\osulazer\\current\\osu!.exe`
- starts `OSUBAND.exe` from the loader directory
- contains a local Guest session placeholder for future website authentication
- performs no DLL injection
