# rpg-map-effects (MVP)

This is a minimal OBS Studio frontend plugin that adds **Tools → RPG Map Effects**.
Click it to open a **standalone window** containing an `obs_display_t` render surface.

**MVP behavior:** the window renders OBS's main texture.  
Next steps: render a chosen battlemap scene + click-to-scene coordinate mapping.

## Build (macOS) — recommended approach

This project is meant to be built **inside the OBS Studio source tree**.

1. Clone OBS Studio (with submodules):
   - `git clone --recursive https://github.com/obsproject/obs-studio.git`

2. Copy this folder to:
   - `obs-studio/plugins/rpg-map-effects`

3. Edit:
   - `obs-studio/plugins/CMakeLists.txt`
   Add this line in alphabetical order with other plugins:
   - `add_obs_plugin (rpg-map-effects PLATFORMS MACOS)`

4. Configure/build OBS Studio as usual (Xcode generator is common on macOS).
   When OBS builds, it will also build this plugin.

5. Run OBS from your build output, then open:
   - **Tools → RPG Map Effects**

## Notes

- This is a frontend plugin (uses `obs-frontend-api.h`) and Qt widgets.
- It intentionally starts as a **floating window**, not a dock, to maximize map real estate.
