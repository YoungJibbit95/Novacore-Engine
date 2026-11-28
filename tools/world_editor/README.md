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

    git fetch origin editor/phase-a-file-playtest
    git worktree add --detach ../Novacore-Editor-Candidate origin/editor/phase-a-file-playtest
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
asset placement, rotation gizmos, Unicode bitmap UI or true docked viewport is
implemented. Native dialogs and subprocess launch require actual interactive acceptance. Do not publish as usable
release until independent visual/technical review and real GPU acceptance.


## Save/open and Play Map

Open and Save As use the native SDL file chooser. Opening another map or closing
with unsaved changes requires an explicit discard confirmation. Cancelling
either leaves the existing document intact. Save As confirms replacing an
existing file. Save stages a sibling file before renaming it to the authorized
destination. Reload refuses dirty documents. Opening a different map clears
the prior save authorization.

Build the Nemisis authored-world candidate (#63) against its exact pinned
engine candidate (#40). In the Nemisis worktree, run the editor executable
using absolute paths, for example:

    & $editorExe --map .\configs\maps\editor-room.world.json --save-as .\configs\maps\my-room.world.json --game $gameExe

Save the map first, then click Play Map. The child process receives only
the configured executable and the saved document via --map, with no shell or
game recompilation. The working directory must be the Nemisis checkout.
The editor will not launch a second playtest until the first exits. Closing
the editor does not terminate a running game. Unsupported assets, missing
spawn or invalid spawn clearance are diagnosed by the actual game loader.

The CPU self-test verifies actual save, overwrite, reopen and rejection of a
malformed file in an isolated temporary directory. Native dialogs, window close
confirmation and launching/playing the game have not been validated here.


## Native smoke (not interactive acceptance)

Configure with -DNOVACORE_REQUIRE_EDITOR_NATIVE=ON in addition to the editor
option to reject a missing SDL3/Vulkan/glslc dependency. Launch with
--vulkan-smoke to require three actual submitted frames with world geometry,
grid lines and UI draw counts. CI runs this on a virtual X11 display with
Mesa lavapipe (software Vulkan), and enables the Khronos validation layer.
This is not physical-GPU, visual, input, HiDPI or Nemisis round-trip acceptance.
