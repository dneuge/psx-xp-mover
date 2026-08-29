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

## DataRefs

The following parameters are exposed as DataRefs to X-Plane and other plugins/addons:

| Name                                                   | Type         | Access\*  | Default             | Description/Meaning                                                                                                                                                 |
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
| `xpmover/suspend_injection`                            | boolean      | writable  | `0` (injecting)     | `1` (true) suspends all injection from PSX to X-Plane (position, orientation, motion vector, ground contact flag, ground speed), `0` reactivates injection          |
| `xpmover/interpolate`                                  | boolean      | writable  | `1` (enabled)       | if `0` (disabled) original values from last received PSX boost frame are applied directly to X-Plane; if `1` (enabled) values get smoothed using a buffer           |
| `xpmover/interpolation_buffer_ms`                      | double       | writable  | `50.0`              | fixed number of milliseconds to "go back in time" and interpolate between an earlier and later received PSX boost frame to smooth movement in X-Plane               |
| `xpmover/interpolation_compensate_time_diff`           | boolean      | writable  | `1` (compensate)    | if `1`, estimated clock drift between simulators (`xpmover/avg_psx_time_diff_ms`) is attempted to be compensated on buffer interpolation; `0` disables compensation |
| `xpmover/avg_psx_time_diff_ms`                         | double       | read-only | `0.0`               | estimated system clock difference between simulators; positive means X-Plane is ahead of PSX, negative means X-Plane is behind PSX                                  |
| `xpmover/interpolation_time_source`                    | integer      | writable  | `1` (RTC)           | selects time source to estimate clock difference: `1` reads time from local real-time clock, `2` uses X-Plane simulation runtime                                    |
| `xpmover/logging/console_level`                        | string\[1]   | writable  | `D` (debug)         | lowest log level to print to console (stdout): `E` Error, `W` Warning, `I` Info, `D` Debug, `T` Trace                                                               |
| `xpmover/logging/xplane_level`                         | string\[1]   | writable  | `I` (informational) | lowest log level to emit to X-Plane log file: `E` Error, `W` Warning, `I` Info, `D` Debug, `T` Trace                                                                |
| `xpmover/plugin/version`                               | string\[256] | read-only | *plugin version*    | indicates the currently running plugin version, unversioned builds identify as `dev`                                                                                |
| `xpmover/plugin/build_id`                              | string\[256] | read-only | *build number*      | ID of CI build that produced the plugin binary; empty if produced without CI                                                                                        |
| `xpmover/plugin/build_ref`                             | string\[256] | read-only | *Git revision*      | revision, tag and modification status to correlate build to Git history                                                                                             |
| `xpmover/plugin/build_target`                          | string\[256] | read-only | *build target*      | information about targeted X-Plane version, system and compiler                                                                                                     |
| `xpmover/plugin/build_time`                            | string\[256] | read-only | *build timestamp*   | date and time the plugin was built at                                                                                                                               |
| `xpmover/connection/hostname`                          | string\[255] | writable  | `localhost`         | hostname of PSX instance to connect to; invalid values are filtered out, modify atomically to avoid connection attempts to unintended hosts                         |
| `xpmover/connection/port`                              | integer      | writable  | `10749`             | TCP port number of PSX instance to connect to                                                                                                                       |
| `xpmover/connection/established`                       | boolean      | read-only | `0` (not connected) | `1` (true) while connected to PSX, `0` (false) while not connected                                                                                                  |

\*) Access column shows intended access only. Read-only marked DataRefs may be still be exposed writable but should not be manipulated by other addons. Manipulation of DataRefs not marked as writable may only be temporary and have no effect.

Note on types:

- booleans are published as integers, `0` means `false`, `1` means `true`
- strings are published as "data" (byte) arrays, terminated by either maximum length or NUL

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

[X-Plane](https://www.x-plane.com/) is a registered trademark of Austin Meyer and Aerosoft.
