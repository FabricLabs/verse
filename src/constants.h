#ifndef CONSTANTS_H
#define CONSTANTS_H
// Storage locations
#ifndef VERSE_STORE_DIR
#define VERSE_STORE_DIR "stores/verse"
#endif
#ifndef VERSE_PLAYER_SAVE
#define VERSE_PLAYER_SAVE "stores/verse/player.save"
#endif

// World dimensions
#define WORLD_SIZE_X 128
#define WORLD_SIZE_Y 128
#define WORLD_SIZE_Z 128

// Convenience macro for uniform cube worlds
#define WORLD_SIZE_CUBE WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z

// World grid constants (for multi-world systems)
#define WORLD_GRID_SIZE 5  // 5x5x5 grid of worlds (radius 2; 125 total)
#define WORLD_GRID_CENTER 2  // Center world index in grid

#ifndef WORLD_AFFINITY
#define WORLD_AFFINITY 12
#endif

// Universe fixed initialization seed (32-byte hex)
#ifndef UNIVERSE_SEED_HEX
#define UNIVERSE_SEED_HEX "e814432fdd116a343e6d7c4dd299b40b528238cbd2799bebff9c8528783ee56c"
#endif

// Temporary startup mode: generate the home island, its cloud layers, and a 5x5 wilderness
// plane (landing cell + two rings) under it. Set to 0 to restore the full 54-world sequence.
#ifndef VERSE_HOME_ONLY_STARTUP
#define VERSE_HOME_ONLY_STARTUP 1
#endif

// Universe Z layer for home worlds (see universe.c WFC vertical model).
#ifndef UNIVERSE_HOME_Z
#define UNIVERSE_HOME_Z 2
#endif

// Audio defaults
#ifndef DEFAULT_MASTER_VOLUME
#define DEFAULT_MASTER_VOLUME 80
#endif

// Time factors for tools/viewers (e.g., accelerated clocks)
#ifndef BASE_TIME_FACTOR
#define BASE_TIME_FACTOR 100
#endif
#ifndef MAX_TIME_FACTOR
#define MAX_TIME_FACTOR 1000ULL
#endif

// Evolution tuning
#ifndef MIN_EPOCHS_FOR_BLOOM
#define MIN_EPOCHS_FOR_BLOOM 5  // require this many epochs before any FLOWERING can bloom
#endif

// Known voxel conditions bitmask (up to 64)
#define MAX_KNOWN_CONDITIONS 64
extern const char* KNOWN_CONDITIONS[];
extern const unsigned int KNOWN_CONDITIONS_COUNT;
unsigned long long condition_bit_from_name(const char* name);
const char* condition_name_from_index(unsigned int idx);

#endif // CONSTANTS_H
