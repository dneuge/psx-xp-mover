# PSX/XP Mover

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

This plugin for [X-Plane](https://www.x-plane.com/) offers a **very basic** injection of position/orientation from
[Aerowinx® Precision Simulator 10](https://aerowinx.com/) (PSX) to an X-Plane (XP) aircraft model, e.g. the default
Boeing 747-400 from X-Plane 11.

This is by no means a proper "scenery generator" and has little use on its own; most of its shortcomings require an
upcoming virtual cockpit bridge. See the section below on what the plugin does and doesn't.
**If you want proper integration with X-Plane *now*, an actual "scenery generator" should be used instead of this
plugin.**

Official repositories are hosted on [Codeberg](https://codeberg.org/dneuge/psx-xp-mover) and
[GitHub](https://github.com/dneuge/psx-xp-mover). Both locations are kept in sync and can be used to submit
pull requests but issues are only tracked on [Codeberg](https://codeberg.org/dneuge/psx-xp-mover/issues) to gather them
in a single place. Please note that [all contributions](CONTRIBUTING.md) incl. issue reports are subject to a
restrictive [AI Usage Policy](AI_USAGE_POLICY.md).

## Scope of this plugin

The original goal was to have some visuals to stay motivated while working on virtual cockpit integration. Since that
took a while longer than expected, the plugin occasionally received some additional features that are also used and
supplemented by the virtual cockpit bridge (not yet released, expected in late 2026).

### What this plugin does

PSX exchanges data over two different interfaces:

- "Main" connections (TCP port 10747) are used for bidirectional exchange of simulator variables,
  updated at variable intervals
- "Boost" connections (TCP port 10749) only emit position and coarse status information from PSX at a higher rate

**This plugin only connects to the "Boost" port** and only updates the aircraft's position in X-Plane, essentially
"dragging it along". While only working in one direction, it also attempts to be "smart" doing that:

- Terrain elevation mismatches between PSX and XP are compensated (more or less successfully) by smoothly blending
  between PSX "in the air" and XP "on the ground". Detection relies on simple thresholds of calculated ground speed
  (too slow to rotate = on ground, faster than expected on ground = airborne), smoothing the transition over time.
- Asynchronous frame updates are attempted to be compensated by interpolating positions over a short buffer.

To be able to do its job, the plugin needs to take control over the complete aircraft and thus disables the
flight model. This may also disable further aspects of the simulation and can cause issues with some other plugins or
external applications (in particular: pilot clients for online networks or virtual airlines - use carefully and
responsibly; please disconnect in case you are causing issues).

### What this plugin doesn't do

Since it **does not connect to the "Main" port** any detail not included in "Boost" frames is unavailable to the plugin.
It also cannot return any information to PSX. Connections to the "Main" port are out of scope due to complexity and
because they are not necessary to be processed with similar near-realtime requirements as position updates. Consider
using either an actual "scenery generator" plugin or the virtual cockpit bridge when it has been released.

Some examples of what the plugin doesn't (and can't) do:

- set external lights in XP \
  => you may be limited to daylight unless you control e.g. landing lights on your own
- control flight surfaces, gear or flaps in XP \
  => external view will look weird (aircraft stuck in some half-initialized state)
- control any part of the cockpit (incl. yoke and thrust lever) or forward any interaction with the cockpit to PSX \
  => internal view will look "stuck"
- X-Plane terrain elevation is not provided back into PSX \
  => terrain mismatch will be more evident in PSX than when such feedback would be provided

## How to install

The plugin should be installed to an *aircraft plugin directory*, not to the global plugin directory. Simply download
and unpack a release version into the `plugins` directory of your *aircraft*, for example as
`Aircraft/Boeing B747-400 XP11/plugins/xpmover`. The plugin has been tested with the default B744 that came with
X-Plane 11. You probably want to create a copy of the aircraft you install this plugin to as you may also need to
disable/delete other plugins/scripts that came with it to ensure that the plugin has full control over the aircraft
model.

## How to build/develop

Please refer to the [Development Guide](DEVELOPMENT.md) for details on how to build the plugin on your own. End users
are recommended to download a binary release version instead.

## Troubleshooting

Some of the following troubleshooting steps need some way to inspect and change DataRefs which requires an additional
plugin or external tool. If you do not have anything installed yet, we recommend Laminar Research's
[DataRefEditor](https://developer.x-plane.com/tools/datarefeditor/).

Note that any changes to DataRefs do not persist and will reset when the plugin is restarted.

### PSX boost frame rate

For this plugin to work as expected, the boost frame output rate in PSX should be set to at least 60Hz without division.
In PSX's Instructor Panel check Preferences/Basics and make sure to select a "Frame rate limit" option that does *not*
include a slash. Options marked e.g. "/3" and "/2" indicate frame rate division. "60/2" limits PSX to 60Hz while boost
frames are reduced to half of that (60Hz/2 = 30Hz). "48/3" even reduces boost frames down to just 16Hz.

At time of writing (PSX version 10.192), 75Hz is the highest and the only undivided option and thus should be used when
connecting this plugin.

### Logging

This plugin logs to both the X-Plane `Log.txt` file as well as to console (stdout), using `[Mover]` prefixes for all
messages. By default, `Log.txt` receives only `INFO` level or more critical messages while console also shows `DEBUG`
messages (first level of developer detail output). Log levels can be reconfigured as needed via `xpmover/logging`
prefixed DataRefs (see table below for details); every lower (more detailed) level also includes all higher (less
detailed) messages.

`DEBUG` level messages may be useful to enable/check if you suspect issues with interpolation/timing. `TRACE` messages
give further insights into internal calculations, while `FINE` messages show full state dumps and thus should only be
activated for a very brief moment.

End-users probably want to reconfigure the `xpmover/logging/xplane_level`, if needed, to read messages from `Log.txt`.
During development, console output (`xpmover/logging/console_level`) usually is more convenient.

To see console log output, simply launch the X-Plane executable from a terminal window. On Windows® you need to pipe the
output to another process to keep the process in foreground, e.g. by running `X-Plane.exe | more` in the old `cmd.exe`
shell (not PowerShell). Windows users may find it easier to raise `xpmover/logging/xplane_level` instead, unless they
need to observe startup messages at `DEBUG` level or below.

### Interpolation issues

Due to both simulators producing frames independently and additionally being subject to network/processing latency,
interpolation is used to smooth movement between PSX boost frames. Interpolation always requires at least 2 PSX boost
frames to be present on the X-Plane side, which at 75Hz in best case (zero latency, full frame rate) should arrive every
13.3ms.

In practice, that ideal minimum delay is impossible to achieve and maintain:

- PSX usually runs with a lower than the maximum frame rate (the configuration option just sets an upper limit)
- PSX is not a real-time application, meaning boost frames leave PSX with random small delays
- boost frames need to pass through network layers; even when both simulators run on the same machine, that adds some small latency
- this plugin needs to receive and (although quick) parse and store the received boost frames
- X-Plane's flight loop callback (at least once per X-Plane render frame) is and can not be synchronized to PSX boost frame arrival times, meaning frames could arrive slightly too late for a flight loop cycle
- running both simulators on the same machine leads to additional resource/scheduling conflicts, reducing frame rates in both applications and thus increasing delays

Through experimentation, the interpolation buffer size (fixed latency to interpolated positions) was determined to
require around 50ms for a smooth experience but the actually required value depends on system load (changing during
flight) and general system performance.

If you notice **frequent stuttering** (possibly buffer underruns), you may need to increase
`xpmover/interpolation_buffer_ms`. `DEBUG` log messages give a clear indication of underruns, however it was not
feasible to leave those messages enabled to be logged to X-Plane's `Log.txt` by default to avoid excessive spamming -
you can still see those messages on console output or by temporarily changing `xpmover/logging/xplane_level` from `I` to
`D`.

If you notice a looping/sliding aircraft (severe underrun beyond 450-500ms) something may be wrong with PSX or the
network connection between both simulators. Unfortunately, such issues cannot be detected by the plugin itself
(PSX boost frames only identify the millisecond part of a second, making time differences beyond 450ms hard to detect).
This may be happening particularly if general performance issues exist; see the list below on what to check.

As a last resort you may want to disable interpolation by setting the `xpmover/interpolate` DataRef to `0`.

### General performance

General points to check if you experience high latency or severe instability with interpolation:

- If you connect to another machine over an actual network, make sure to use a wired connection (LAN) instead of
  wireless (WiFi/WLAN).
- Avoid exchanging boost frames over the Internet; instead sync PSX instances via the "Main" port and connect to your
  local PSX instance for Boost frames instead.
- Fully quit all applications not related to flight simulation to reduce overall system load.
- You may need to reduce the number of worker threads used by X-Plane by passing a `--num_workers=N` argument to the
  X-Plane executable (replace `N` by the desired number, e.g. number of "performance core" CPU threads minus 2).
- Avoid on-the-fly ortho-generation for X-Plane as it is known to severely impact system performance. Delegate such
  tasks to dedicated machines or simply generate static tiles ahead of time.
- Try if limiting X-Plane framerate improves/stabilizes overall performance by passing a `--lock_fr=N` argument to the
  X-Plane executable (replace `N` by the desired number of frames per second, e.g. `30`).
- If you use Windows, make sure that "game mode" is disabled (otherwise the X-Plane main process may get prioritized too
  high, restricting all other applications, including PSX, to impossibly low CPU usage). This may require using "hacks"
  such as [Compatibility Manager](https://github.com/nbusseneau/CompatibilityManager).
- If you use an asymmetrical CPU check that process affinity is configured correctly; both simulators should run on
  "performance cores" and may benefit from larger caches (e.g. Ryzen X3D). Configuration depends on your exact CPU model.
- Try to set your CPU into a "performance" energy profile. Some CPUs (Ryzen) may only use that request as a hint, yet
  it should be tried in case of performance issues.

## DataRefs

The following parameters are exposed as DataRefs to X-Plane and other plugins/addons:

| Name                                                   | Type         | Access    | Default             | Description/Meaning                                                                                                                                                 |
|--------------------------------------------------------|--------------|-----------|---------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `xpmover/model_offset/length`                          | double       | writable  | `28.194`            | distance (meters) between PSX flight deck reference point and X-Plane aircraft model origin along the fuselage                                                      |
| `xpmover/model_offset/height`                          | double       | writable  | `3.8`               | height difference (meters) between PSX flight deck reference point and X-Plane aircraft model origin                                                                |
| `xpmover/debug/spin_hdg`                               | double       | writable  | `0.0` (disabled)    | spins the aircraft if non-zero (unit: seconds per revolution); useful to check model offset                                                                         |
| `xpmover/terrain/always_update_elevation_info`         | boolean      | writable  | `0` (false)         | if `1` (true) X-Plane terrain elevation is always probed; `0` (false) probes only if needed (close to ground)                                                       |
| `xpmover/terrain/elevation_msl_meters`                 | double       | read-only | NaN                 | X-Plane terrain elevation above mean sea level (meters) underneath the aircraft, determined by terrain probe (NaN while unavailable)                                |
| `xpmover/terrain/remaining_cycles`                     | integer      | read-only | `0`                 | expiration countdown to clear last probed X-Plane terrain elevation after probes are no longer needed                                                               |
| `xpmover/terrain/blending/suspend`                     | boolean      | writable  | `0` (not suspended) | `1` (true) always applies unadjusted PSX altitude without terrain elevation blending, even while on ground                                                          |
| `xpmover/terrain/blending/ground_contact_cycles`       | integer      | read-only | `0` (not on ground) | number of joint frame evaluation cycles PSX reported the aircraft to have ground contact (maximum is limited)                                                       |
| `xpmover/terrain/blending/ground_contact_fraction`     | double       | read-only | `0.0`               | transition between ground/airborne: `1.0` means "long enough in ground contact", `0.0` means "long enough in flight"                                                |
| `xpmover/terrain/blending/firm_ground_speed`           | double       | writable  | `60.0`              | approximate ground speed (knots) at which the aircraft should be firmly pinned to ground (rotation impossible)                                                      |
| `xpmover/terrain/blending/lift_ground_speed`           | double       | writable  | `160.0`             | approximate ground speed (knots) at which the aircraft should have left ground (i.e. too fast to not have rotated)                                                  |
| `xpmover/terrain/blending/low_speed_fraction`          | double       | read-only | `0.0`               | transition between "firm" (`1.0`) and "lift" (`0.0`) ground speeds                                                                                                  |
| `xpmover/terrain/blending/low_speed_fraction_factor`   | double       | writable  | `0.8` (80%)         | controls the effect of `xpmover/terrain/blending/low_speed_fraction` on overall blending fraction                                                                   |
| `xpmover/terrain/blending/elevation_blending_fraction` | double       | read-only | `0.0`               | blends between XP and PSX elevations; `1.0` fully pins to X-Plane terrain, `0.0` fully applies PSX value; smoothed output                                           |
| `xpmover/ground_speed_calculated`                      | double       | read-only | `0.0`               | ground speed (knots) observed in X-Plane, calculated from summed distances between boost frames (Haversine great circle)                                            |
| `xpmover/publish/ground_speed_calculated`              | boolean      | writable  | `1` (enabled)       | attempts to write calculated ground speed to generic X-Plane datarefs if enabled (`1`); does not work (X-Plane limitation)                                          |
| `xpmover/publish/motion_vector`                        | boolean      | writable  | `1` (enabled)       | writes motion vector (`local_vx`, `local_vy`, `local_vz`) to X-Plane if enabled (`1`); may only take full effect while flight model is *active*                     |
| `xpmover/psx/flightdeck_latitude`                      | double       | read-only | NaN                 | PSX latitude (degrees) of flight deck reference point; latest position from PSX while injection is active; transformed X-Plane coordinates while suspended          |
| `xpmover/psx/flightdeck_longitude`                     | double       | read-only | NaN                 | PSX longitude (degress) of flight deck reference point; latest position from PSX while injection is active; transformed X-Plane coordinates while suspended         |
| `xpmover/psx/elevation_msl_m`                          | double       | read-only | NaN                 | PSX elevation (meters) of flight deck reference point; latest position from PSX while injection is active; transformed X-Plane coordinates while suspended          |
| `xpmover/suspend_injection`                            | boolean      | writable  | `0` (injecting)     | `1` (true) suspends all injection from PSX to X-Plane, re-enabling XP's flight model; `0` reactivates injection, taking back full control                           |
| `xpmover/interpolate`                                  | boolean      | writable  | `1` (enabled)       | if `0` (disabled) original values from last received PSX boost frame are applied directly to X-Plane; if `1` (enabled) values get smoothed using a buffer           |
| `xpmover/interpolation_buffer_ms`                      | double       | writable  | `50.0`              | fixed number of milliseconds to "go back in time" and interpolate between an earlier and later received PSX boost frame to smooth movement in X-Plane               |
| `xpmover/interpolation_compensate_time_diff`           | boolean      | writable  | `1` (compensate)    | if `1`, estimated clock drift between simulators (`xpmover/avg_psx_time_diff_ms`) is attempted to be compensated on buffer interpolation; `0` disables compensation |
| `xpmover/avg_psx_time_diff_ms`                         | double       | read-only | `0.0`               | estimated system clock difference between simulators; positive means X-Plane is ahead of PSX, negative means X-Plane is behind PSX                                  |
| `xpmover/interpolation_time_source`                    | integer      | writable  | `1` (RTC)           | selects time source to estimate clock difference: `1` reads time from local real-time clock, `2` uses X-Plane simulation runtime                                    |
| `xpmover/logging/console_level`                        | string\[1]   | writable  | `D` (debug)         | lowest log level to print to console (stdout): `E` Error, `W` Warning, `I` Info, `D` Debug, `T` Trace, `F` Fine Trace                                               |
| `xpmover/logging/xplane_level`                         | string\[1]   | writable  | `I` (informational) | lowest log level to emit to X-Plane log file: `E` Error, `W` Warning, `I` Info, `D` Debug, `T` Trace, `F` Fine Trace                                                |
| `xpmover/plugin/version`                               | string\[256] | read-only | *plugin version*    | indicates the currently running plugin version, unversioned builds identify as `dev`                                                                                |
| `xpmover/plugin/build_id`                              | string\[256] | read-only | *build number*      | ID of CI build that produced the plugin binary; empty if produced without CI                                                                                        |
| `xpmover/plugin/build_ref`                             | string\[256] | read-only | *Git revision*      | revision, tag and modification status to correlate build to Git history                                                                                             |
| `xpmover/plugin/build_target`                          | string\[256] | read-only | *build target*      | information about targeted X-Plane version, system and compiler                                                                                                     |
| `xpmover/plugin/build_time`                            | string\[256] | read-only | *build timestamp*   | date and time the plugin was built at                                                                                                                               |
| `xpmover/connection/hostname`                          | string\[256] | writable  | `localhost`         | hostname of PSX instance to connect to; invalid values are filtered out, modify atomically to avoid connection attempts to unintended hosts                         |
| `xpmover/connection/port`                              | integer      | writable  | `10749`             | TCP port number of PSX instance to connect to                                                                                                                       |
| `xpmover/connection/established`                       | boolean      | read-only | `0` (not connected) | `1` (true) while connected to PSX, `0` (false) while not connected                                                                                                  |

Note on types:

- booleans are published as integers, `0` means `false`, `1` means `true`
- strings are published as "data" (byte) arrays, terminated by either maximum length or NUL

### Suspending injection to re-enable X-Plane's flight model

While the plugin is enabled, it deactivates X-Plane's flight model in order to position the aircraft exactly as
indicated by PSX. DataRef `xpmover/suspend_injection` can be used to return control to X-Plane. While injection is
suspended, flightdeck position DataRefs no longer reflect data received from PSX, instead they are also "reversed" to
indicate the position of the flightdeck as present in X-Plane. An external tool is required to make use of that feature.
The upcoming virtual cockpit bridge uses that feature to steer PSX while a push-back truck
(via [BetterPushback](https://codeberg.org/skiselkov/BetterPushbackC)) is connected in X-Plane.

## Disclaimer

In addition to the disclaimer already present in the [MIT license](LICENSE.md) we would like to issue a few specific
warnings:

Aircraft position and orientation are subject to sudden and/or discontinuous changes. You are highly discouraged
from using its output (i.e. any state injected to X-Plane) to drive motion platforms or other hardware interfaces as
that could greatly increase risks of injury or damage.

Even when using PSX in a certified setup, this plugin should not be used for real-world training. The project is being
developed for recreation and thus may not qualify for professional use and could void your certification.
In addition to the cockpit not being handled by this plugin, inaccurate representation should be expected at all times,
including drift by interpolation or terrain blending, affecting critical flight phases in particular (takeoff,
departure, approach, landing) as well as inaccurate depiction of ground operations (e.g. wrong acceleration and
object clearance).

As the license already states, no warranty or liability will be given; use at your own risk.

## License

All sources and original files of this project are provided under [MIT license](LICENSE.md), unless declared otherwise
(e.g. by source code comments). Please be aware that dependencies (e.g. libraries and/or external data used by this
project) are subject to their own respective licenses which can affect distribution, particularly in binary/packaged
form.

Binary builds are subject to an [additional license](licenses/xpmover-binary-distribution.txt). Release builds include
a `LICENSES.txt` file combining all license information in a single file. Users accept all license terms and disclaimers
by downloading and installing the plugin to their flight simulator.

### Note on the use of/for AI

Usage for AI training is subject to individual source licenses, there is no exception. This generally means that proper
attribution must be given and disclaimers may need to be retained when reproducing relevant portions of training data.
When incorporating source code, AI models generally become derived projects. As such, they remain subject to the
requirements set out by individual licenses associated with the input used during training. When in doubt, all files
shall be regarded as proprietary until clarified.

Unless you can comply with the licenses of this project you obviously are not permitted to use it for your AI training
set. Although it may not be required by those licenses, you are additionally asked to make your AI model publicly
available under an open license and for free, to play fair and contribute back to the open community you take from.

For contributions to this project, **AI tools and services may only be used as detailed in the
[Contribution Guidelines](CONTRIBUTING.md) and separate [AI Usage Policy](AI_USAGE_POLICY.md)**. Contributors will be
asked to confirm and permanently record compliance with that policy. Violations may lead to immediate and permanent
removal from this project, depending on severity.

## Acknowledgements

[Aerowinx](https://aerowinx.com/) is a registered trademark of Hardy Heinlin.

Microsoft and Windows are trademarks of the [Microsoft](https://www.microsoft.com/) group of companies.

[X-Plane](https://www.x-plane.com/) is a registered trademark of Austin Meyer.
