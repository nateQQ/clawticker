# Icons

`firmware/src/icons.c` holds alpha-only (A8) icons that the firmware recolors at runtime, at 12, 16 and 40 px. They are rasterized by `tools/gen_icons.py` from `assets/logos/claude.svg` and `assets/logos/codex.svg` (from [lobe-icons](https://github.com/lobehub/lobe-icons), MIT).

The Claude and Codex marks are trademarks of Anthropic and OpenAI. They are used only to label which service a card shows. To change them, replace the SVGs and run `python3 tools/gen_icons.py` (needs `cairosvg` and `pillow`), then rebuild.
