#ifndef CHARACTER_H
#define CHARACTER_H

#include "world.h"
#include "state_transition.h"
#include "actor.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

// Player ID structure following BIP 143 double SHA256 specification
// TODO: let this define the official Fabric Message structure; all "types" in our system are Fabric Messages.
typedef struct {
    uint8_t magic[4];      // Magic bytes (0xc0d3f33d)
    uint8_t version[4];    // Version (little endian)
    uint8_t parent[32];    // Parent hash (32 bytes)
    uint8_t author[32];    // Author hash (32 bytes)
    uint8_t type[4];       // Type (little endian)
    uint8_t size[4];       // Size (little endian)
    uint8_t hash[32];      // Content hash (32 bytes)
    uint8_t signature[64]; // Signature (64 bytes)
} __attribute__((packed)) PlayerIdPreimage;

// Player ID (double SHA256 hash of preimage)
typedef struct {
    uint8_t id[32];        // Double SHA256 hash
    PlayerIdPreimage preimage; // Original preimage data
} PlayerId;

// Player structure that inherits from Actor
typedef struct {
    Actor base;                    // Inherit from Actor
    int gold;                      // Player-specific: total copper in purse (legacy field name)
    char world_seed[65];           // Player-specific: world seed
    char save_timestamp[32];       // Player-specific: save timestamp
    StateTransitionSystem* state_transitions; // Player-specific: state transition log
    PlayerId player_id;            // Player-specific: Player ID following BIP 143 specification
} Player;

// Character save structure (legacy, will be replaced by Player)
// TODO:
// - rename `gold` to `energy`
// - rename `experience_points` to `experience`
typedef struct {
    char name[64];
    int x, y, z;  // Position
    int strength, dexterity, intelligence, wisdom, constitution, luck;
    int experience_points;
    int gold; // Total copper in purse (shown as copper/silver/gold)
    char world_seed[65];  // Hex string for world seed
    char save_timestamp[32];  // ISO timestamp
    bool is_valid;  // Flag to mark valid saves
    StateTransitionSystem* state_transitions;  // State transition log
    PlayerId player_id;  // Player ID following BIP 143 specification
    // Appended: older saves omit trailing fields; loaders zero-fill and treat missing as 0.
    int attribute_points;
    int skill_points;
    uint32_t unlocked_skills;
    uint8_t skill_ranks[32];
} CharacterSave;

// Character save/load functions
bool character_save_game(const char* character_name, int x, int y, int z,
                        int strength, int dexterity, int intelligence,
                        int wisdom, int constitution, int luck,
                        int experience_points, int gold, const char* world_seed,
                        int attribute_points, int skill_points, uint32_t unlocked_skills,
                        const uint8_t *skill_ranks);

bool character_load_game(const char* character_name, CharacterSave* save);

// Load a specific file rather than the character's latest slot. The browser needs this: every
// timestamped backup is a distinct save, and character_load_game always opens latest.save.
bool character_load_from_path(const char *path, CharacterSave *save);

bool character_delete_save(const char* character_name);

// Remove one save file. Does not touch other backups or latest.save unless that is the path given.
bool character_delete_save_file(const char *path);

// How many saves the browser will hold. Timestamped backups accumulate; this is a display cap, not
// a disk cap — older files stay on disk and simply do not appear until a nearer one is deleted.
#define CHARACTER_SAVE_MAX_LIST 32
#define CHARACTER_SAVE_PATH_MAX 256

// One on-disk save, as the browser shows it. data is the same blob character_save_game writes, so
// loading from path is the same as loading from latest.save — only the filename differs.
typedef struct {
    char path[CHARACTER_SAVE_PATH_MAX];
    char filename[80];
    CharacterSave data;
    time_t mtime;
} CharacterSaveEntry;

// Fill out[] with valid .save files in characters/, newest first. Returns the number written.
int character_list_save_files(CharacterSaveEntry *out, int max);

// List all saved characters
int character_list_saves(CharacterSave* saves, int max_saves);

// Check if a character save exists
bool character_save_exists(const char* character_name);

// Check if any save files exist
bool character_any_saves_exist();

// Save to default save file
bool character_save_default(const char *character_name, int x, int y, int z,
                           int strength, int dexterity, int intelligence,
                           int wisdom, int constitution, int luck,
                           int experience_points, int gold, const char *world_seed,
                           int attribute_points, int skill_points, uint32_t unlocked_skills,
                           const uint8_t *skill_ranks);

// Load from default save file
bool character_load_default(CharacterSave *save);

// Check if default save exists
bool character_default_save_exists();

// Player ID functions following BIP 143 specification
PlayerId* player_id_create(const char* character_name, const char* world_seed, uint32_t version);
void player_id_destroy(PlayerId* player_id);
bool player_id_validate(const PlayerId* player_id);
char* player_id_to_hex(const PlayerId* player_id);
PlayerId* player_id_from_hex(const char* hex_string);
bool player_id_equals(const PlayerId* id1, const PlayerId* id2);

// Preimage manipulation functions
PlayerIdPreimage* player_id_preimage_create(uint32_t version, const char* parent_hash,
                                          const char* author_hash, uint32_t type,
                                          uint32_t size, const char* content_hash,
                                          const char* signature);
void player_id_preimage_destroy(PlayerIdPreimage* preimage);
uint8_t* player_id_preimage_serialize(const PlayerIdPreimage* preimage, size_t* size);
PlayerIdPreimage* player_id_preimage_deserialize(const uint8_t* data, size_t size);

// Double SHA256 hash function (BIP 143 compliant)
void double_sha256_hash(const uint8_t* input, size_t input_len, uint8_t* output);

// Player functions (new Actor-based system)
Player* player_create(const char* name, const char* description, const char* world_id, const char* world_seed);
void player_destroy(Player* player);
bool player_save(Player* player, const char* filename);
Player* player_load(const char* filename);
bool player_validate(const Player* player);

// Get character save filename
void character_get_save_filename(const char* character_name, char* filename, int max_len);

// Get timestamped save filename
void character_get_timestamped_save_filename(const char* character_name, char* filename, int max_len);

// Log available save files
void character_log_save_files();

// Validate character save data
bool character_validate_save(const CharacterSave* save);

// Create characters directory if it doesn't exist
bool character_ensure_directory();

#endif // CHARACTER_H
