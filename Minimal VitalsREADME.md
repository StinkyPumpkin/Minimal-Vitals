# Minimal Vitals

A small vertical health / magicka / stamina widget for the HUD, drawn through SKSE Menu Framework.
It replaces TrueHUD's player bars. TrueHUD's boss bars, enemy bars and enchantment meters are untouched.

## What it shows

- **Frame**: one thin-line frame with a diagonal divider. All diagonals are 30 degrees off vertical. The top edge rises to a point on the right; the divider and the bottom edge fall to the right.
- **Gold band**: inside each box, a band of Edge UI gold fading to black runs between the frame and the bar track. The track itself stays clear, so an emptying bar leaves empty space.
- **Health**: fills up from the divider.
- **Magicka and stamina**: sit side by side under the divider and fill down from it.
- **Numbers**: current values beside the frame. The bars are a fixed size at every level; only the numbers grow.
- **Casting**: a pink-to-purple glow grows clockwise round the lower box while a spell charges, starting at its top-left corner. It shines outward from behind the frame line; along the divider it is drawn over the health section. Once the spell is fully charged and still held (or a concentration spell is being channelled), the whole box glows and pulses. It disappears when the spell is released or cancelled. The charge timer and hand priority follow zax's Casting Bar.
- **Shout cooldown**: a white glow grows from the top point down the top edge, then down the whole left side, while the shout recovers, then disappears. It is wider than the cast glow so both show where they share the lower left edge. Its ends are cut parallel to the frame.

## Requirements

- SKSE, Address Library, SKSE Menu Framework.

## Settings

In SKSE Menu Framework, open **Minimal Vitals**.

- **Layout**: position, size, rotation (upright, 90, 180 or 270 degrees; lying flat the numbers sit in one row along the long edge, and they always stay upright), line width, opacity, number size, grain, and the casting and shout indicators. **Preview** shows the widget all the time and loops a fake cast and shout cooldown, so you can place it without casting. Preview is not saved.
- **Visibility**: a hide hotkey and the follow-compass-hide option. The widget also hides with iHUD Smart Hide, TFCam free camera, Photo Mode Hide UI, menus and loading screens.

Settings are saved to `SKSE/Plugins/MinimalVitals/config.json` (created on the first change, never shipped).

## Vanilla bars and TrueHUD

TrueHUD is not required. Minimal Vitals does not hide the vanilla bars itself; use TrueHUD, iHUD or any HUD mod that hides them.

With TrueHUD: set its player widget Health, Magicka, Stamina and Shout indicator display to **Never**.
Keep **Hide vanilla bars** on. If the vanilla bars still show, turn on **Force hide vanilla bars**.

## Files

- `SKSE/Plugins/MinimalVitals.dll`
- `Interface/MinimalVitals/grain.dds`: 128 px tiling grain (made by `tools/make_grain.py`).
- `SKSE/Plugins/Fonts/MinimalVitals.ttf`: the number font, Outfit Regular (SIL Open Font License, `Minimal Vitals Font OFL.txt`; source `art/`). SKSE Menu Framework loads every font in that folder.

## License

Code: GPL-3.0 (`LICENSE`). Font: Outfit, Copyright 2021 The Outfit Project Authors, SIL OFL 1.1.
