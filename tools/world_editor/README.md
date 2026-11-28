# NovaCore World Editor — first native candidate

This is a Phase A development candidate, not a release or a playable Nemisis map
loader. The executable uses the existing SDL3 window and Vulkan world renderer;
it refuses the null and SDL debug fallbacks. The compact dark hierarchy,
inspector and toolbar follow the starter concept, using the renderer's current
bitmap overlay rather than adding an unvalidated GUI dependency. Panels overlay
the full camera framebuffer (not an independent docked viewport).

## Windows graphical build (separate worktree)

From the engine checkout, with Visual Studio 2022 C++ tools, Vulkan SDK including
glslc, and SDL3 available (or FetchContent access):

    git fetch origin editor/phase-a-edit-controls
    git worktree add --detach ../Novacore-Editor-Candidate origin/editor/phase-a-edit-controls
    cd ../Novacore-Editor-Candidate
    cmake --preset windows-msvc-debug -DNOVACORE_BUILD_WORLD_EDITOR=ON
    cmake --build --preset windows-msvc-debug --target novacore_world_editor
    .\build\windows-msvc-debug\Debug\novacore_world_editor.exe --save-as .\room.world.json

For a headless compile and CPU contract check (not a graphical test):

    cmake --preset local-debug-no-deps -DNOVACORE_BUILD_WORLD_EDITOR=ON
    cmake --build --preset local-debug-no-deps --target novacore_world_editor
    ctest --test-dir build/local-debug-no-deps -R novacore_world_editor --output-on-failure

## Manual graphical acceptance (not yet executed)

1. Launch the candidate. Confirm the actual Vulkan backend is logged, the
   floor, wall, partition, crate and marker are visible, and the grid is drawn.
2. Hold RMB inside the viewport to look and fly (WASD, Q/E); release to select.
   LMB a box and check the hierarchy/inspector selection. Click hierarchy rows.
3. Click Add Box, use inspector +/- for XYZ position and half extents (0.25 m
   per step), and toggle the collision flag. Click an inspector value, type a number, and press Enter (Esc cancels).
   Drag a coloured axis tip to translate with 0.25 m snap. Test Undo/Redo,
   Duplicate and Delete. Rotation is deliberately unsupported.
4. Click Save, verify the file, then click Reload. Close and relaunch with
   --map .\room.world.json --save-as .\room.world.json, confirming stable IDs
   and saved transforms. Save replaces only the explicit --save-as path,
   after writing a temporary sibling file. Reload refuses dirty documents.
5. Resize, minimize/restore, switch focus and close. Verify pointer selection
   stays aligned with the Vulkan framebuffer, including HiDPI.
6. Capture screenshots and run with Vulkan validation layers. Confirm no
   validation errors, failed resource lifetime operations or input capture leaks.

The generic marker is drawn as a cross; unresolved asset references appear in
the hierarchy with an explicit status, not as fake meshes. No Blender import,
asset placement, rotation gizmos, file picker, Unicode bitmap UI, quit
confirmation dialog, true docked viewport, or Nemisis runtime loading is
implemented. Unsaved close warns on stderr only. Do not publish as usable
release until independent visual/technical review and real GPU acceptance.
