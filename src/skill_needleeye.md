---
name: needleeye
description: >
  Drive the Needleeye 3D Earth globe desktop app via its CLI and Session D-Bus
  interface (`org.needleeye.App`): fly the camera, switch base maps, toggle
  3D terrain or day/night shading, hide the HUD, load KML/KMZ/CZML files or
  live feeds, scrub timelines, query visible features, and capture screenshots.
---

# Driving Needleeye

Needleeye exposes its live window over Session D-Bus (`org.needleeye.App` at
`/org/needleeye/App`) and wraps it in a `--json` CLI on the `needleeye` binary.
Always pass `--json` when scripting or inspecting structured output. Exit code
is `0` on success and `1` with an error on stderr.

## Quick Reference

```sh
# Check if Needleeye is running and inspect current state
needleeye status --json

# Camera control (use --instant before taking a screenshot)
needleeye fly 35.6762 139.6503 --alt 800 --pitch 35 --heading 15 --instant --json
needleeye reset north --json
needleeye reset home --json

# Base maps & visual toggles
needleeye map list --json
needleeye map set esri-satellite --json       # accepts id, name, or enabled slot 1-9
needleeye set --terrain on --day-night on --grid off --hud off --json

# KML / KMZ / CZML feeds and local files
needleeye feed list --json
needleeye feed add /tmp/custom_events.czml --name "Mission" --json
needleeye feed add https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_week_age.kml --json
needleeye feed enable gdacs-live --json
needleeye feed disable usgs-week-25 --json
needleeye feed refresh --json

# Query & select features, scrub timeline
needleeye features --query "Japan" --limit 20 --json
needleeye select "ISS" --json
needleeye select none --json
needleeye timeline --active on --progress 0.5 --pause --json

# Capture the live OpenGL globe window directly to PNG/JPG
needleeye screenshot /tmp/globe.png --json

# Offline inspection (no running GUI needed)
needleeye inspect path/to/file.czml --json
```

## Common Agent Workflow: Plot Custom Data & Verify Visually

1. Generate a `.kml` or `.czml` file in `/tmp/` (use CZML for time-dynamic
   tracks, 3D orbital altitudes, or ellipses; use KML for placemarks/polygons).
2. Load it into the running window: `needleeye feed add /tmp/plot.czml --json`.
3. Frame the shot cleanly:
   `needleeye set --hud off --day-night on --json`
   `needleeye fly <lat> <lon> --alt <km> --instant --json`
4. Capture and inspect the result: `needleeye screenshot /tmp/view.png --json`.
5. Restore HUD if needed: `needleeye set --hud on --json`.

If `needleeye status --json` reports that Needleeye is not running on the
session D-Bus, ask the user to launch `needleeye` (or launch it as a detached
desktop app) before issuing live commands.
