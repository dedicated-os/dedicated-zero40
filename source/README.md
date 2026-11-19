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
Cycle display mode (Off -> x2 -> Hi-Res -> Off) - hold MENU and press START<br>
Save screenshot - hold MENU and press A<br>
Toggle fast-forward - press L3

MENU + START cycles Off -> x2 -> Hi-Res -> Off. The selected mode appears near
the center-right of the game screen for three seconds, including after a restart
or relaunch. The mode is saved in `userdata/settings.bin`; older settings retain
their preferences and inherit the existing Hi-res ON/OFF selection.

Off uses nearest 2x scaling when cropped; uncropped screens keep the original
scaling filter (normally linear from 256x192 to 480x360). Off and x2 therefore
look the same when cropped. x2 keeps DraStic at 256x192 and uses nearest
scaling to 512x384: cropped screens clip 16 display pixels on each side; uncropped
screens use a reusable 512x384 render target, then linear scaling to 480x360.
Off -> x2 takes effect immediately. Snapshots in both modes remain native 256x192
per screen. If a render target is unavailable, uncropped x2 falls back to direct
nearest scaling and logs the failure.

Entering or leaving Hi-Res saves the current resume state and restarts DraStic.
Hi-Res selects the software renderer by default and sets `hires_3d = 1`,
`threaded_3d = 1` and `disable_edge_marking = 1` (existing game-specific exceptions
still apply). Off and x2 select OpenGL ES 2.0 and set these options to zero.
`DRASTIC_RENDERER` continues to override renderer selection in all three modes.

MENU + L2 turns both crops OFF when both are ON; otherwise it turns both ON. The individual screen settings are saved, and existing shared crop settings carry over to both screens.

With Hi-Res and the software renderer, each screen whose crop is OFF uses a fixed 15/16 nearest-neighbor copy from 512x384 into a 480x360 texture, then draws at 1:1. AArch64 uses NEON; other builds use fixed-size copies. A screen whose crop is ON keeps the original 512x384 image and clips its sides.

Sleep/wake - press and release POWER<br>
Shutdown - press and hold POWER for 1 second

Relaunch DraStic - open menu and press START<br>
Kill DraStic - hold MENU and press POWER

While charging:

Wake display - press any button (except POWER)<br>
Launch game - press and release POWER<br>
Poweroff - press and hold POWER for 1 second

To check the shrink copy, run `make test` in this directory inside the aarch64 toolchain container. It checks both the NEON and portable implementations against a full-image reference with different pitches and unaligned buffers. When cross-compiling on another architecture, set `TEST_RUNNER` to an appropriate emulator.

Game screen uploads use an owned CPU frame and draw game pixels as opaque.
GLES2 Hi-Res ARGB8888 inputs use a supported 32-bit XRGB texture (SDL's
`SDL_PIXELFORMAT_RGB888`); its shader ignores DraStic's broken alpha bytes,
avoiding a CPU alpha rewrite on every frame. Other paths, or XRGB creation
failures, retain CPU alpha correction. `DRASTIC_OPAQUE_TEXTURE=0` disables this
optimization for comparison. The game texture log reports the selected format
and alpha handling. Both manual screenshots and save/menu snapshots
use that frame and save RGB24 BMPs, without re-locking streaming textures to read
old pixels. Native resolution is preserved. Menus and other UI textures retain
their transparency.

Run `make test-screenframe` with the aarch64 toolchain and SDL2 development files
to check alpha correction, RGB preservation, padded/unaligned input, partial
updates, BMP round-trips and frame recreation. For a host build, for example,
use `make test-screenframe CROSS_COMPILE=/usr/bin/` when `/usr/bin/gcc` and SDL2
are available. `SDL2_CONFIG`, `SDL2_TEST_CFLAGS`, `SDL2_TEST_LIBS` and `TEST_RUNNER`
can be overridden for a different installation or emulator.

Run `make test-displaymode` with the same SDL2/toolchain options to check the
mode cycle, restart decisions, nearest x2 cropping, two-pass scaling, restoration
of render state and filters, live texture updates and intermediate texture cleanup.

To test Hi-res with GLES after this correction, set `DRASTIC_RENDERER=opengles2`
in DraStic's environment (or `DRASTIC_RENDERER=software` for comparison). The
default renderer selection above is retained pending device validation. The
selected renderer, SDL video backend, window/output sizes and game texture
formats are logged by `system/run.sh` in
`/mnt/SDCARD/userdata/logs/drastic-YYYYMMDD-HHMMSS-PID.log`.
Each launch, including automatic mode-change restarts, creates a separate file;
both stdout and stderr (including the AV profile) are captured. Read the files
from `userdata/logs/` after mounting the card. If the directory cannot be written,
the launcher falls back to `/tmp/drastic.txt` so the game can still start.

See [the investigation and device checks](RENDERING_INVESTIGATION.ja.md) for
the bundled DraStic's NEON alpha defects and the remaining display-path questions.

For a Hi-Res GLES upload comparison, `DRASTIC_UPLOAD_BUFFERS=2` alternates between
two opaque game textures per screen. It displays the texture just updated and
keeps screenshots on the current CPU image. The default is one texture; extra
texture allocation failure also falls back to one. The game texture log reports
`upload_buffers=1` or `2`. Compare total render time and audio shortages, since
driver waits may move between stages rather than disappear. Initial DQ4 device
logs showed slightly faster uploads but more audio shortages with two textures;
use one texture for the current detailed investigation. Use `=1` or remove the
export to restore the single-texture path.

For Hi-res audio noise investigation, set `DRASTIC_PROFILE=2` before starting
DraStic. The current diagnostic `main.sh` sets this and `DRASTIC_UPLOAD_BUFFERS=1`
explicitly; change the profile value to `0`
after the investigation. Each launch log begins with the renderer/profile
environment values, so a missing export is visible immediately.
It logs render-stage timings, callback intervals and, for the validated
bundled binary, audio ring shortage observations every five seconds. Audio
format/rate/buffer requests and obtained values, renderer VSync flags and swap
interval are also logged at startup. No audio rate or buffer-size override is
applied. See [the audio investigation and measurement guide](AUDIO_INVESTIGATION.ja.md).

Profile level `1` retains the basic render/audio measurements. Level `2` adds
thread CPU versus off-CPU time, time outside the Present hook, game pixel writing
between Lock/Unlock, overlay/input/device work, and SDL_Delay caller statistics.
Per-frame pipeline totals merge overlapping spans instead of adding per-screen
averages. Every five seconds it also reads Linux per-thread CPU/scheduler
statistics and available CPU frequency/thermal sysfs values. No extra file I/O
or CPU-clock calls are added on the audio callback thread. Off-CPU includes
sleeping, blocking and scheduling delays; it does not directly identify a 3D
condition-variable wait. Worker CPU measurements are separate from main-thread
CPU time. Unavailable readings are labelled rather than treated as zero.

The gameplay battery/charging overlay polls its two power-supply sysfs files on
the first frame and then once per second, instead of every frame. Battery warning
updates can take up to one second. Detailed profiling reports actual reads as
`power_poll` (included in `overlay`). Sleep/power handling and charging-screen
polls retain their direct reads. `make test-powerstatus` checks polling boundaries,
charge/low-battery changes, long pauses and the 32-bit tick counter wrapping.

`make test-screenframe-portable` checks alpha correction with NEON disabled;
`make test-avprofile` checks callback passthrough, timing, ring observations and
deferred reporting. `make test-avprofile-detail` checks CPU-clock spans, overlap
accounting, delay attribution and unavailable clocks; `make test-avsystem`
checks Linux task parsing, deltas, TID reuse and unavailable system readings.
`make test-gametexture` checks opaque texture selection and
fallbacks, RGB layout compatibility, and display/BMP preservation with broken
alpha, using the software renderer locally. It does not validate the device's
GLES driver. Use the same SDL2/toolchain options as the other tests.

`make test-gameupload` checks alternating latest frames, partial-image
preservation, filters, opaque RGB, upload/allocation failure and texture ownership
with the software renderer.
