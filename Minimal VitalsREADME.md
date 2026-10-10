# Minimal Vitals

A small vertical health / magicka / stamina widget for the HUD, drawn through SKSE Menu Framework.
It replaces TrueHUD's player bars. TrueHUD's boss bars, enemy bars and enchantment meters are untouched.

## What it shows

- **Frame**: one thin-line parallelogram with a diagonal divider. All diagonals are 45 degrees.
- **Health**: fills up from the divider.
- **Magicka and stamina**: sit side by side under the divider and fill down from it.
- **Numbers**: current values beside the frame. The bars are a fixed size at every level; only the numbers grow.
- **Casting**: an orange line runs clockwise round the lower box while a spell charges, starting at its top-left corner. Once the spell is fully charged and still held (or a concentration spell is being channelled), the whole outline pulses. It disappears when the spell is released or cancelled. The charge timer and hand priority follow zax's Casting Bar.
- **Shout cooldown**: a white line runs down the upper right edge from the top corner to the divider while the shout recovers, then disappears.

## Requirements

- SKSE, Address Library, SKSE Menu Framework.

## Settings

In SKSE Menu Framework, open **Minimal Vitals**.

- **Layout**: position, size, line width, opacity, number size, grain, and the casting and shout indicators. **Preview** shows the widget all the time and loops a fake cast and shout cooldown, so you can place it without casting. Preview is not saved.
- **Visibility**: a hide hotkey and the follow-compass-hide option. The widget also hides with iHUD Smart Hide, TFCam free camera, Photo Mode Hide UI, menus and loading screens.

Settings are saved to `SKSE/Plugins/MinimalVitals/config.json` (created on the first change, never shipped).

## TrueHUD

Set TrueHUD's player widget Health, Magicka, Stamina and Shout indicator display to **Never**.
Keep **Hide vanilla bars** on. If the vanilla bars still show, turn on **Force hide vanilla bars**.

## Files

- `SKSE/Plugins/MinimalVitals.dll`
- `Interface/MinimalVitals/grain.dds`: 128 px tiling grain (made by `tools/make_grain.py`).
- `SKSE/Plugins/Fonts/MinimalVitals.ttf`: the number font. SKSE Menu Framework loads every font in that folder.
  The current file is Century Gothic copied from Windows for personal use. Replace it with an OFL font such as Jost before any public release.
