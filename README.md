## RPG Map Effects – OBS battlemap helper

`RPG Map Effects` is an OBS Studio **frontend plugin** that adds a tool window under  
**Tools → RPG Map Effects** for running tactical battle maps with:

- **Battlemap scene picker** linked to Program output
- **Grid overlay** (preview + optional on-stream image overlay)
- **FX template scenes** (named `FX: Something`) you can spawn onto the map
- **Sequenced FX** using `[250] name`–style delays
- **Per‑template defaults** (fade, lifetime, label, sequencing)

Everything lives inside this plugin; it uses only public OBS APIs and does **not** require
patching OBS core.

---
![SCR-20260305-mpfe](https://github.com/user-attachments/assets/924e8cc2-3766-4287-9349-76c99455cb3d)


## Features (current)

- **Battlemap view**
  - Choose a battlemap scene from a dropdown; the plugin window and Program scene follow it.
  - Battlemap scenes are any scenes whose name starts with `Map:` (case‑insensitive). Only those appear in the dropdown.
  - Click on the map display to drive FX placement and selection.

- **Grid overlay**
  - Preview-only grid (in the plugin window) with:
    - Cell size (px)
    - Line width (px)
    - Color
  - **Show grid on output**:
    - Creates/ensures an image source named `RPG Map Grid`.
    - Renders a PNG at the base canvas size and adds it as a source to the selected battlemap scene.
    - Places the grid source on top of other sources.
    - Checkbox on → grid visible; off → grid hidden (item stays in the scene).

- **FX template scenes**
  - Any scene named like `FX: Something` (case‑insensitive `fx:` prefix) is treated as an **effect template**.
  - You can pick an FX template, then:
    - Click on the map to **spawn** an instance into the current battlemap.
    - Configure:
      - **Sequence [ms]** using `[250] name` prefixes in the template to stage child items.
      - **Label text** (optional text inside the FX scene, moves with the effect; label text and color are configurable, and labels start hidden until you show them).
      - **Fade‑out** duration (ms) for a hide transition when clearing.
      - **Lifetime** (seconds, `0` = infinite) to auto‑clear.
  - Per‑template defaults (sequence, label, fade, lifetime) are saved per template UUID.

- **Active effects list**
  - Shows all currently active FX instances on the map.
  - Lets you:
    - Select FX (also via click‑nearest on the map).
    - Clear selected / last / all, with configured fade‑out where applicable.
  - Double‑click / edit labels in the list to rename instances; labels update the in‑scene text.

- **Set direction (click‑to‑face)**
  - Check **Set direction (click map)**, then click on the map to set the **selected** effect’s rotation so it “faces” that point.
  - Dragging with Set direction enabled, or in right‑click **Rotate** lock mode, shows a cyan arrow and continuously rotates the selected FX toward the mouse while you move.
  - Useful for characters or directional effects; rotation is applied to the FX scene item.

- **Cursor overlay**
  - Right‑click on the map preview to access **Show cursor / Hide cursor** for the current battlemap.
  - When shown, a cursor image (from a toolbar **Cursor** dropdown) is added as an `image_source` named `RPG Map Cursor`, centered on the map and locked to the mouse position while the pointer is over the preview.
  - Cursor images are embedded PNG assets (e.g. `cursor`, `pointer`, `target`, `double`, `clicks`); the toolbar dropdown selects which asset is written to disk and used for the cursor source.
  - The cursor is managed **per battlemap**: each `Map:` scene can have its own cursor item. On plugin open/refresh, any existing cursors in battlemap scenes are hidden so the preview starts clean.

---

## Building inside the OBS source tree (macOS)

The plugin is designed to build **inside an OBS Studio checkout**.

1. **Clone OBS Studio** (with submodules):

   ```bash
   git clone --recursive https://github.com/obsproject/obs-studio.git
   ```

2. **Clone this plugin into the `plugins/` folder** of that checkout:

   ```bash
   cd obs-studio/plugins
   git clone https://github.com/adiastra/RPG-Map-Effects.git rpg-map-effects
   ```

3. **Wire the plugin into OBS CMake**  
   Edit `obs-studio/plugins/CMakeLists.txt` and add, in the plugins section:

   ```cmake
   add_obs_plugin(rpg-map-effects PLATFORMS MACOS)
   ```

4. **Configure / build OBS** as usual (e.g. Xcode generator on macOS).  
   When OBS builds, it will also build the `rpg-map-effects` plugin.

5. **Run OBS** from your build output and open:

   - **Tools → RPG Map Effects**

---

## Usage overview

1. **Select your battlemap scene**
   - Use the scene dropdown at the top of the window.
   - The plugin preview switches, and OBS Program is set to that scene.

2. **Configure and show the grid**
   - Adjust cell size, line width, and color.
   - Check **Show grid** to see the preview grid in the plugin window.
   - Check **Show grid on output** to add the `RPG Map Grid` image source to your battlemap
     scene and show it on stream/recording.

3. **Create FX template scenes**
   - In OBS, make scenes named `FX: Something` and build your effect using normal sources.
   - Optional sequencing: prefix child source names with `[250]`, `[1000]`, etc. to delay their
     reveal in milliseconds.

4. **Spawn FX onto the map**
   - Choose a template in the **Template Scene** combo.
   - Configure sequence / label / fade / lifetime as desired.
   - Click on the map display and use **Spawn at last click** to drop the effect.

5. **Manage active effects**
   - Use the **Active Effects** list to select, rename, and clear effects.
   - Clear buttons support per‑effect fade‑out when configured.
   - Editing an effect’s label in the list updates the in‑scene label text (and keeps the configured label color). Labels start hidden by default; use context menus (FX list or map) to show/hide them.

6. **Show or hide the cursor overlay**
   - Use the toolbar **Cursor** dropdown to choose a cursor style.
   - Right‑click on the map preview and choose **Show cursor** to add the cursor overlay to the current `Map:` scene; move the mouse over the preview to reposition it.
   - Right‑click again and choose **Hide cursor** to hide the cursor in that battlemap.

---

## Notes

- This is a **frontend plugin** that uses `obs-frontend-api.h` and Qt widgets.
- The UI is a **floating window**, not a dock, to maximize map real estate.
- All scene and source manipulation uses the public OBS API; no OBS core patches are required.

