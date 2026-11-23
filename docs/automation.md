# Needleeye Automation: D-Bus, CLI, and Agent Skill

Needleeye is designed to be driven externally by shell scripts, Hyprland keybindings, desktop tools, and AI agents **without embedding any agent or MCP runtime into the app**.

```text
  Human / Script / AI Agent (reads SKILL.md)
                      │
                      ▼   needleeye <cmd> ... --json
             [ needleeye CLI ]  (same binary, headless)
                      │
                      ▼   Session D-Bus (org.needleeye.App)
           [ Running Needleeye GUI ]
                      ▲
                      │   (optional, zero Needleeye code)
            [ dbus-mcp bridge ]  ◄── MCP-only clients
```

---

## 1. Agent Skill (`SKILL.md`)

A concise agent skill is compiled directly into the `needleeye` binary. Install it into your agent's skill directory with:

```sh
needleeye skill install              # auto-detects ~/.gemini/config/skills, ~/.claude/skills, ~/.agents/skills
needleeye skill install --dir /path  # install into a specific directory
needleeye skill show                 # print bundled SKILL.md to stdout
```

---

## 2. CLI Reference

All automation commands accept `--json`, exit `0` on success or `1` on error, and print errors to `stderr`.

### Live Window Commands (via Session D-Bus)

| Command | Description |
| :--- | :--- |
| `needleeye status [--json]` | Camera pose, active base map, display toggles, feature counts, timeline state, and current selection. |
| `needleeye fly <lat> <lon> [--alt <km>] [--pitch <deg>] [--heading <deg>] [--instant] [--json]` | Move camera to coordinates. Omit `--instant` for smooth animation; pass `--instant` before `screenshot`. |
| `needleeye reset [north\|home] [--json]` | Reset pitch/heading to North-up or return to the default orbital view. |
| `needleeye map list [--json]` | List all base maps (`id`, `name`, `projection`, `enabled`, `active`, `slot`, `maxZoom`). |
| `needleeye map set <id\|name\|slot> [--json]` | Activate a base map by ID (`esri-satellite`), name (`Blue Marble`), or enabled slot (`1`–`9`). |
| `needleeye set [--terrain on\|off] [--grid on\|off] [--day-night on\|off] [--atmosphere on\|off] [--hud on\|off] [--sidebar on\|off] [--json]` | Toggle one or more display/HUD flags in a single call. |
| `needleeye feed list [--json]` | List configured KML/KMZ/CZML feeds and opened local files. |
| `needleeye feed add <file\|url> [--name <label>] [--json]` | Open a local `.kml`/`.kmz`/`.czml` file or add a remote feed URL. |
| `needleeye feed enable <id\|name> [--json]` | Enable a feed. |
| `needleeye feed disable <id\|name> [--json]` | Disable a feed. |
| `needleeye feed remove <id> [--json]` | Remove a feed or local file. |
| `needleeye feed refresh [--json]` | Refresh all active feeds. |
| `needleeye features [--query <text>] [--limit <n>] [--json]` | List or filter currently visible features (default limit `50`, max `500`). |
| `needleeye select <id\|name\|none> [--no-fly] [--json]` | Select a feature (and fly to it unless `--no-fly` is set), or pass `none` to clear selection. |
| `needleeye timeline [--active on\|off] [--progress <0..1>] [--play\|--pause] [--json]` | Control timeline filtering and playback for timestamped KML/CZML feeds. |
| `needleeye screenshot <output.png> [--json]` | Capture the live composited 3D globe window atomically to PNG or JPG. |
| `needleeye quit [--json]` | Quit the running Needleeye window cleanly. |

### Offline Commands (No GUI Required)

| Command | Description |
| :--- | :--- |
| `needleeye inspect <file.kml\|kmz\|czml> [--limit <n>] [--json]` | Parse and summarize a local KML, KMZ, or CZML file offline. |
| `needleeye skill show` | Print the bundled `SKILL.md`. |
| `needleeye skill install [--dir <path>]` | Install the bundled `SKILL.md`. |

---

## 3. Session D-Bus Interface (`org.needleeye.App`)

- **Bus:** Session (`QDBusConnection::sessionBus()`)
- **Service:** `org.needleeye.App`
- **Object Path:** `/org/needleeye/App`
- **Interface:** `org.needleeye.App`

Every method takes primitive D-Bus types (`s`, `d`, `b`, `i`) for straightforward `busctl` and `dbus-mcp` introspection, and returns a compact JSON string (`{"ok":true,...}` or `{"ok":false,"error":"..."}`).

| Method | Signature | Description |
| :--- | :--- | :--- |
| `GetState` | `() -> s` | Full state snapshot (camera, map, display, HUD, counts, timeline, selection). |
| `FlyTo` | `(ddd) -> s` | Animated fly to `(lat, lon, altKm)`. Pass `altKm <= 0` to keep current altitude. |
| `SetView` | `(dddddb) -> s` | Set `(lat, lon, altKm, pitch, heading, instant)`. Pass `NaN` for unchanged pitch/heading. |
| `ResetNorth` | `() -> s` | Reset pitch & heading to `0°`. |
| `ResetHome` | `() -> s` | Reset to default orbital view. |
| `ListBaseMaps` | `() -> s` | List all configured base maps. |
| `SetBaseMap` | `(s) -> s` | Activate base map by ID, name, or slot string. |
| `SetTerrain` | `(b) -> s` | Enable/disable 3D AWS Terrarium DEM terrain. |
| `SetGrid` | `(b) -> s` | Enable/disable graticule grid. |
| `SetDayNight` | `(b) -> s` | Enable/disable real-time solar terminator shading. |
| `SetAtmosphere` | `(b) -> s` | Enable/disable atmospheric limb halo. |
| `SetHudHidden` | `(b) -> s` | Hide/restore all HUD chrome (globe-only mode). |
| `SetSidebarVisible` | `(b) -> s` | Show/hide the feeds & layers sidebar. |
| `ListFeeds` | `() -> s` | List all feeds and opened files. |
| `LoadSource` | `(ss) -> s` | Load `(pathOrUrl, optionalName)`. |
| `SetFeedEnabled` | `(sb) -> s` | Enable/disable feed by ID or name. |
| `RemoveFeed` | `(s) -> s` | Remove feed by ID. |
| `RefreshFeeds` | `() -> s` | Refresh all enabled feeds. |
| `ListFeatures` | `(si) -> s` | Search/list visible features `(query, limit)`. |
| `SelectFeature` | `(sb) -> s` | Select `(idOrName, flyTo)`. Empty or `"none"` clears selection. |
| `SetTimeline` | `(bdb) -> s` | Set `(active, progress, playing)`. Pass `progress < 0` to leave unchanged. |
| `CaptureScreenshot` | `(s) -> s` | Grab the live window framebuffer atomically to `filePath`. |
| `Quit` | `() -> s` | Quit the running GUI cleanly. |

### Calling Directly with `busctl`

```sh
# Introspect the live service
busctl --user introspect org.needleeye.App /org/needleeye/App

# Fly to Reykjavik at 600 km and switch to Earth at Night
busctl --user call org.needleeye.App /org/needleeye/App org.needleeye.App FlyTo ddd 64.1466 -21.9426 600
busctl --user call org.needleeye.App /org/needleeye/App org.needleeye.App SetBaseMap s earth-night
```

### Bridging to MCP (Optional)

Because `org.needleeye.App` uses standard D-Bus introspection and primitive arguments, any universal D-Bus ↔ MCP bridge (such as `dbus-mcp`) can expose Needleeye's methods directly to MCP-only clients without modifying Needleeye.
