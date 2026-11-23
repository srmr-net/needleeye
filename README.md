# Needleeye

A native C++ / Qt 6 Quick 3D Earth globe and live KML/KMZ situational viewer built for Omarchy.

- **Zero API keys & customizable base maps:** Streams free public base maps (Dark Tactical, Esri Aerial, EOxCloudless Sentinel-2, OpenStreetMap, **Earth at Night**, **NASA Blue Marble**, OpenTopoMap, Voyager, Esri Topo) plus any custom **Tiled (XYZ / TMS)** tile server or **single-image world map** (**Equirectangular** / **Web Mercator**, remote URL or local file) configured in Settings (`,`), with 3D terrain elevation (AWS Terrarium DEM) and persistent local disk caching.
- **Built for live KML, KMZ & CZML feeds:** Native `QXmlStreamReader` + `libzip` + JSON engine supporting KML/KMZ (`Document`, `Folder`, recursive `NetworkLink`, `Placemark`, `Style`/`StyleMap`, `TimeStamp`/`TimeSpan`, `LookAt`) and **CZML** (`document`/`clock`, `cartographicDegrees`/`cartographicRadians`/`cartesian` ECEF, time-sampled 3D orbital/flight trajectories, `point`, `billboard`, `path`, `polyline`, `polygon`, `ellipse`, `availability`, and `properties`) — preloaded with live USGS Earthquake and GDACS Global Disaster feeds.
- **Omarchy native:** Follows `~/.local/state/omarchy/current/theme/colors.toml`, Hyprland corner rounding (`decoration:rounding`), Omarchy language switching, xdg-desktop-portal file opening, and Wayland app packaging.

## Build & Install

```sh
./bin/build      # builds build/needleeye
./bin/test       # runs Qt Test suite offscreen
./bin/install    # packages and installs via makepkg -fsi
```

You can also pass local `.kml`/`.kmz`/`.czml` files or feed URLs directly on launch, or drive a running instance from scripts and AI agents via CLI / Session D-Bus (`org.needleeye.App`):

```sh
./build/needleeye path/to/events.czml
./build/needleeye status --json
./build/needleeye fly 64.1466 -21.9426 --alt 600 --instant --json
./build/needleeye set --day-night on --hud off --json
./build/needleeye screenshot /tmp/globe.png --json
./build/needleeye skill install
```

See [docs/automation.md](docs/automation.md) for the complete D-Bus interface (`org.needleeye.App`), CLI reference, and agent skill documentation.

## Keyboard Shortcuts

| Key | Action |
| :--- | :--- |
| `/` | Jump to loaded KML feature, city, or place (Photon geocode) |
| `L` | Toggle feeds & layers sidebar |
| `H` | Hide or restore all HUD panels (globe-only mode) |
| `O` | Open local `.kml` / `.kmz` file |
| `R` | Refresh active KML feeds |
| `Space` | Play / pause timeline (or refresh feeds) |
| `[` / `]` | Step timeline backward / forward |
| `1` – `9` | Switch enabled base maps |
| `,` / `Ctrl+,` | Open settings & base map manager |
| `T` / `G` / `D` | Toggle 3D terrain elevation / graticule grid / day-night shading |
| `Arrows` | Pan globe (`Shift + Arrows` to tilt horizon & rotate heading) |
| `+` / `-` | Zoom in / out |
| `N` / `0` | Reset north / home orbital view |
| `Right-drag` | Tilt horizon & rotate heading |
| `?` | Show or hide shortcut overlay |
| `Q` | Quit |
