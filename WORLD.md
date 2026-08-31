# World Generation
VERSE uses procedural world generation to create unique and engaging game spaces for its players while maintaining a consistent style and experience.

## Generation
We use Wave Function Collapse to ensure consistent tiles across 3 layers of depth:

- Layer 1: large scale features
- Layer 2: regions and biomes
- Layer 3: local strata and appearance

Wilderness biomes are **not** separate WFC tile types. On wilderness cells (universe Z = 0), a continuous climate field (temperature, moisture, elevation, volcanic intensity) is sampled at absolute universe coordinates and classified into eight soft-blended biomes: temperate forest, grassland, boreal, desert, wetland, alpine, tropical, and volcanic. That field drives surface vegetation, ambient weather, host-rock ore provinces, and fauna spawn tables so neighboring worlds stay seamless.

The game world centers on a persistent shared multiplayer space, dedicated "home worlds" for each user, and ephemeral "battle worlds" which exist only for the duration of their specified game rules.

### World Types
- Solid Voxel Fill (can be empty)
- Home ("Island in the Sky")
- Wilderness
- Underworld
- Arena
- Outpost
- Sky (upper portion filled with clouds)

### World Rules
Tiles computed in w, x, y, z:

- A "home" world tile must be surrounded by "empty" world tiles
- A "wilderness" tile can have two kinds of lateral neighbors: wilderness, outpost
- A "wilderness" tile must have a "sky" tile immediately above it
- A "wilderness" tile must have a "solid bedrock" tile immediately below it
- An "empty" tile can have a "sky" tile below it

Individual worlds derive entropy from a shared noise field comprised of 3 Perlin applications in varying sizes, large to small, enabling run-time generation of deterministic "chunks" of the universe (we call these chunks "worlds" as they contain additional properties such as ownership).

## Data Structures
- Universe
  - holds seed
  - contains worlds in an x, y, z grid
  - each world also contains a `w` coordinate defaulting to 0
- World
  - 32x32x32 units of voxels
  - can contain voxels which reference other worlds
