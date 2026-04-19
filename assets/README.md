# Pre-boot splash assets

These files feed `scripts/build_assets.py`, which runs during PlatformIO
pre-build and emits C headers under `src/generated/`. The firmware then
compiles those headers in, so anything dropped here ends up baked into the
binary (no SD-card dependency at splash time).

## Expected files

| Path | Purpose | Notes |
| --- | --- | --- |
| `BgTile.png` | Tiled desktop background | Small square (e.g. 16x16 / 32x32 / 64x64). Drawn at 2x nearest-neighbor across the whole screen. |
| `happymacicon.png` | Centered boot icon | PNG with alpha. Drawn at 2x nearest-neighbor; transparent pixels are skipped. |
| `fonts/chicago.bdf` | Chicago proportional bitmap font | BDF format, any size. Used by the settings UI and any splash overlays. |

All three are optional. When a file is missing, the script bakes in a classic
built-in default (50%-gray tile, 32x32 Happy Mac, 5x7 ASCII font), so the
firmware always builds and runs even with nothing dropped in yet.

## Regenerating manually

```sh
python3 scripts/build_assets.py --force
```

or simply rebuild the PlatformIO project; the hook re-runs whenever a source
file's mtime is newer than its generated header.
