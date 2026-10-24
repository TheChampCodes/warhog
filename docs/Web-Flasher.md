# Web Flasher

A zero-install browser flasher (`web-flasher/`) built on **ESP Web Tools** (WebSerial).
It serves one manifest per board and shows an SVG preview of each device's screen.

- **Live:** https://olosnoks.github.io/warhog (GitHub Pages)
- Needs a Chromium browser (Chrome/Edge) for WebSerial.
- The page footer shows the build version `<ref>+<sha7>` (e.g. `main+a1b2c3d`) — confirm
  it matches the commit you expect before flashing.

## Layout
```
web-flasher/
├── index.html       the installer page (board cards, SVG screen previews)
├── pig.js           page logic
├── screens.js       the SVG device-screen previews
├── manifests/*.json one ESP Web Tools manifest per board
├── firmware/*.bin   merged images, filled in by CI at deploy time
└── ham/             HAM-edition assets
```

## Manifests
Each `manifests/<env>.json` points at a single merged image flashed at offset `0`:
`esp32c3_oled`, `freenove-s3-cyd-ham-ui4`, `guition-jc8048w550`, `warhognode_c5`,
`waveshare_c6`, plus `xiao_c5`. The `*-ham` manifests
(`xiao_c5-ham`, `pigletnode_c5-ham`, `tdongle_c5-ham`) point at **upstream Hamspiced
release** binaries, not this fork's build — don't use them to flash this fork's firmware.

## How a build reaches the flasher
CI (`build-and-deploy.yml`) builds every env, merges each with `esptool merge_bin`, copies
the results into `web-flasher/firmware/`, substitutes the `__VERSION__` token in the page
and manifests, and publishes `web-flasher/` to Pages. See [Build & Flash](Build-and-Flash.md)
for the pipeline and how to dispatch a redeploy.

## Choosing a role on a fresh flash
A freshly flashed **display board defaults to Core** (`meshModeOnBoot = "core"`); the C5/C3
**node firmwares run as workers** and auto-search for a Core. Change the role later from the
touch UI (display boards) or `/wardriver.cfg`.
