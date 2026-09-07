# OSU!BAND

OSU!BAND is the lazer-only continuation of the working LAME runtime port for osu!lazer 2026.804.2.

## What changed

- Product/project/executable renamed to `OSUBAND`.
- osu!stable client code, offsets and stable attach path removed.
- Original gameplay modules are preserved byte-for-byte from the known-working LAME baseline:
  Aim Assist, Relax, Tap Assist, Replay Bot and Autobot.
- New animated Dear ImGui interface with a custom OSU!BAND identity.
- Lazer runtime status, bindings and config profiles kept.

## Controls

- Start osu!lazer.
- Start `OSUBAND.exe`.
- `F4` toggles the menu by default.

## Profile card

The menu contains a user-card slot. It currently uses a safe local placeholder rather than guessing at an unverified `API.LocalUser` memory layout. The current osu! source exposes the logged-in user through `API.LocalUser`, so the card is ready for a later verified profile bridge without touching gameplay modules.

## Build

Open `OSUBAND.sln` and build `Release | x64`, or run the included GitHub Actions workflow.

