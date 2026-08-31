# VERSE Agent Guidance
This file provides guidance to artificallly intelligent agents when working on VERSE.

## General Overview
VERSE is a multiplayer roguelike built on a powerful voxel engine which supports highly-optimized rendering and manipulation of voxel worlds.

## Development
- Use `make` to compile the project

## Release
- **Native**: `make release-check-native` or `npm run release:verify:native` (headless C tests + `verse-client`).
- **Full stack**: `npm run release:verify` (`npm ci`, webpack build, `test:ci` without Puppeteer). Run `npm test` locally for browser integration tests.
- **Version**: Root `VERSION` should match `package.json` for tagged releases.

## TODO
- [ ] valgrind pass
