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
- control flight surfaces, gear or flaps in XP\ 
  => external view will look weird (aircraft stuck in some half-initialized state)
- control any part of the cockpit (incl. yoke and thrust lever) or forward any interaction with the cockpit to PSX \
  => internal view will look "stuck"
- X-Plane terrain elevation is not provided back into PSX
  => terrain mismatch will be more evident in PSX than when such feedback would be provided

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
