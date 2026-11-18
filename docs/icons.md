# Icons

`firmware/src/icons.c` holds alpha-only (A8) icons that the firmware recolors at runtime: a radial burst for Claude and a terminal prompt for Codex, in 12, 16 and 40 px. They are simple stand-ins drawn by `tools/gen_icons.py`, not the vendors' official logos.

To use official marks, edit `tools/gen_icons.py` (or write your own `icon_burst*` / `icon_term*` arrays with the same names and sizes), run it, and rebuild. Check each vendor's brand guidelines before redistributing their logos.
