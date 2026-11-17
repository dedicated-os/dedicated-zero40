The MENU button is labeled G / HOME / BACK on the device.

Confirm - press A<br>
Cancel - press B

Open menu - press MENU

Open/close lid - L2
Simulate mic input - R2

Toggle top screen crop - hold MENU and press UP or Y<br>
Toggle bottom screen crop - hold MENU and press DOWN or B<br>
Toggle crop for both screens - hold MENU and press L2<br>
Toggle gap - hold MENU and press R2<br>
Toggle Hi-res 3D - hold MENU and press START<br>
Save screenshot - hold MENU and press SELECT<br>
Toggle fast-forward - press L3

Hi-res 3D changes the SDL renderer, so DraStic saves the current resume state and restarts after the toggle. Hi-res ON selects the software renderer and sets `threaded_3d = 1` and `disable_edge_marking = 1`; Hi-res OFF selects OpenGL ES 2.0 and sets `threaded_3d = 0` and `disable_edge_marking = 0`. All three options are saved together.

MENU + L2 turns both crops OFF when both are ON; otherwise it turns both ON. The individual screen settings are saved, and existing shared crop settings carry over to both screens.

With Hi-res ON, each screen whose crop is OFF uses a fixed 15/16 nearest-neighbor copy from 512x384 into a 480x360 texture, then draws at 1:1. AArch64 uses NEON; other builds use fixed-size copies. A screen whose crop is ON keeps the original 512x384 image and clips its sides.

Sleep/wake - press and release POWER<br>
Shutdown - press and hold POWER for 1 second

Relaunch DraStic - open menu and press START<br>
Kill DraStic - hold MENU and press POWER

While charging:

Wake display - press any button (except POWER)<br>
Launch game - press and release POWER<br>
Poweroff - press and hold POWER for 1 second

To check the shrink copy, run `make test` in this directory inside the aarch64 toolchain container. It checks both the NEON and portable implementations against a full-image reference with different pitches and unaligned buffers. When cross-compiling on another architecture, set `TEST_RUNNER` to an appropriate emulator.
