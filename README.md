# V E R S E
✨

## Quick Start
```
npm i
npm start
```

Visit http://localhost:9998 to view the demo application.

## Features
- **Open-source development** — want to change or alter the game?  [Clone the source][verse-clone-the-source].
- Peer-to-peer networking
- User-defined behaviors
- AI-assisted design
- Over 90 million human contributions
- Origin story 2005

### Fabric Client
Run `fabric` to interact with the network.

### Contract
By default, `@verse/core` will check for a `contract` property in `settings/local.[js|json]` for an existing contract, else it will proceed with attempting to deploy using the local network.

## Settings
- `contract` — Fabric Contract ID
- `http` — Fabric HTTP Settings (passed directly to `@fabric/http`)
- `peer` — Fabric Peer list (connect by default)

[verse-clone-the-source]: NEWBIE.md
