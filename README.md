# Needleeye

A native C++ / Qt 6 Quick 3D Earth globe and live KML/KMZ situational viewer built for Omarchy.

- **Zero API keys & customizable base maps:** Streams free public base maps (Dark Tactical, Esri Aerial, EOxCloudless Sentinel-2, OpenStreetMap, **Earth at Night**, **NASA Blue Marble**, OpenTopoMap, Voyager, Esri Topo) plus any custom **Tiled (XYZ / TMS)** tile server or **single-image world map** (**Equirectangular** / **Web Mercator**, remote URL or local file) configured in Settings (`,`), with 3D terrain elevation (AWS Terrarium DEM) and persistent local disk caching.
- **Built for live KML & KMZ feeds:** Native `QXmlStreamReader` + `libzip` engine supporting `<Document>`, `<Folder>`, recursive `<NetworkLink>` polling, `<Placemark>` (`Point`, `LineString`, `Polygon`, `MultiGeometry`, `gx:Track`), `<Style>`/`<StyleMap>`, `<TimeStamp>`/`<TimeSpan>`, and `<LookAt>` — preloaded with live USGS Earthquake and GDACS Global Disaster feeds.
- **Omarchy native:** Follows `~/.local/state/omarchy/current/theme/colors.toml`, Hyprland corner rounding (`decoration:rounding`), Omarchy language switching, xdg-desktop-portal file opening, and Wayland app packaging.

## Build & Install

```sh
./bin/build      # builds build/needleeye
./bin/test       # runs Qt Test suite offscreen
./bin/install    # packages and installs via makepkg -fsi
```

You can also pass local `.kml`/`.kmz` files or feed URLs directly on launch:

```sh
./build/needleeye path/to/events.kmz
```

## Keyboard Shortcuts

| Key | Action |
| :--- | :--- |
| `/` | Jump to loaded KML feature, city, or place (Photon geocode) |
| `L` | Toggle feeds & layers sidebar |
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
