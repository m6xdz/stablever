# OSU!BAND

OSU!BAND is the lazer-only continuation of the working LAME runtime port for osu!lazer 2026.804.2.

## What changed

- Product/project/executable renamed to `OSUBAND`.
- osu!stable client code, offsets and stable attach path removed.
- Stable gameplay surface contains Aim Assist, Relax, Replay and Stable Cloud Configs.
- Tap Assist and AutoBot have been removed from the Stable source tree, project and config schema.
- New animated Dear ImGui interface with a custom OSU!BAND identity.
- Lazer runtime status, bindings and config profiles kept.

## Controls

- Start osu!lazer.
- Start `OSUBAND.exe`.
- `F4` toggles the menu by default.

## Profile card

The menu user card is populated from the authenticated OSU!BAND account session: avatar, nickname and subscription information.

## Build

Open `OSUBAND.sln` and build `Release | x64`, or run the included GitHub Actions workflow.

