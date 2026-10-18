# Minimal Vitals

A small health / magicka / stamina widget for Skyrim SE / AE, drawn through SKSE Menu Framework.
Thin-line frame, fixed size at every level, current values beside it.

**Download: [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/195787)**

![Minimal Vitals](docs/main.jpg)

## Features

- Health fills up from the divider; magicka and stamina fill down, side by side
- Bars never grow with level; only the numbers change
- Glow round the lower box while a spell charges; pulses when fully charged or channelling
- Glow down the frame while your shout recovers
- Rotate it 90, 180 or 270 degrees; the numbers stay upright
- Hides in menus, loading screens, with a hotkey, with Photo Mode Hide UI, and whenever the compass is hidden
- Position, size, rotation, opacity, line width and glows set in SKSE Menu Framework, with a live preview

## Requirements

- SKSE
- Address Library for SKSE Plugins
- SKSE Menu Framework

TrueHUD is not required. Minimal Vitals does not hide the vanilla bars itself; use TrueHUD, Immersive HUD or any HUD mod that hides them.

Full user notes: [Minimal VitalsREADME.md](Minimal%20VitalsREADME.md) (the file shipped with the mod).

## Building

- Visual Studio 2022 or Build Tools (C++ workload), CMake, Ninja
- vcpkg with `VCPKG_ROOT` set (CommonLibSSE-NG comes from the registry in `vcpkg-configuration.json`)
- Edit the two paths at the top of `build.bat` to match your Visual Studio install, then run it. The DLL lands in `build/release/`.
- Optional: set `SKYRIM_MODS_FOLDER` to your MO2 `mods` folder and the build copies the DLL into a mod folder there.

Release layout: `SKSE/Plugins/MinimalVitals.dll`, `SKSE/Plugins/Fonts/MinimalVitals.ttf` (from `art/`),
`Interface/MinimalVitals/grain.dds` (from `art/`, made by `tools/make_grain.py`).

## Credits

- zax: [Casting Bar](https://github.com/zax-ftw/CastingBar) (Apache-2.0), the cast timing approach
- Outfit font: The Outfit Project Authors, SIL Open Font License 1.1 (`art/Minimal Vitals Font OFL.txt`)
- CommonLibSSE-NG, SKSE Menu Framework

## License

GPL-3.0 (`LICENSE`). Free and open: modify, fork and reupload as you like.
