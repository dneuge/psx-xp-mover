# Security Policy

## Supported Versions

As long as the plugin is in "development preview" state (with no versions officially released for end-users), only the
latest trunk version (i.e. head revision of `main` Git branch) is supported.

## Attack Vectors

This project is executed as a plugin within the X-Plane (XP) flight simulator, connecting to an instance of the Aerowinx
Precision Simulator 10 (PSX) flight simulator over TCP. It receives a continuous stream of position & status information
from PSX and applies it to the user's aircraft model in XP. The plugin also exchanges information over so-called
datarefs (shared variables) within X-Plane.

Attack vectors could be maliciously crafted dataref or PSX data, however this would require a malicious actor to
already have some control over the victim's machine or flight simulation network.

## Reporting Security Issues

As impact is expected to be extremely limited in the current scope of the plugin, you may simply file a publicly visible
[issue on the official repository](https://codeberg.org/dneuge/psx-xp-mover/issues).

**Note that all reports made to the issue tracker are immediately visible to the public** (Full Disclosure).

If you think you found a security issue that actually warrants to be kept secret, please report to the current project
maintainer via the email address visible in the Git history after cloning a local copy or on the website linked in the
user profile.

Please check this file for future updates.
