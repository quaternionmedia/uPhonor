#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "uphonor.h"

/* Configuration system for saving/loading uphonor state */

/* Configuration file format version */
#define CONFIG_VERSION "1.0"

/* Default configuration filename */
#define DEFAULT_CONFIG_FILENAME "uphonor_session.json"

/* Configuration result codes */
typedef enum
{
  CONFIG_SUCCESS = 0,
  CONFIG_ERROR_FILE_NOT_FOUND = -1,
  CONFIG_ERROR_PARSE_FAILED = -2,
  CONFIG_ERROR_WRITE_FAILED = -3,
  CONFIG_ERROR_INVALID_VERSION = -4,
  CONFIG_ERROR_MEMORY = -5,
  CONFIG_ERROR_INVALID_DATA = -6
} config_result_t;

/* Configuration save/load functions */

/**
 * Save the current uphonor state to a JSON configuration file
 * @param data Pointer to the main data structure
 * @param filename Path to save the configuration file (NULL for default)
 * @return CONFIG_SUCCESS on success, error code on failure
 */
config_result_t config_save_state(struct data *data, const char *filename);

/**
 * Load uphonor state from a JSON configuration file
 * @param data Pointer to the main data structure to populate
 * @param filename Path to the configuration file (NULL for default)
 * @return CONFIG_SUCCESS on success, error code on failure
 */
config_result_t config_load_state(struct data *data, const char *filename);

/**
 * Save only the active loops (those with recorded content) to configuration
 * @param data Pointer to the main data structure
 * @param filename Path to save the configuration file (NULL for default)
 * @return CONFIG_SUCCESS on success, error code on failure
 */
config_result_t config_save_active_loops_only(struct data *data, const char *filename);

/**
 * Validate a configuration file without loading it
 * @param filename Path to the configuration file
 * @return CONFIG_SUCCESS if valid, error code if invalid
 */
config_result_t config_validate_file(const char *filename);

/**
 * Get a human-readable error message for a config result code
 * @param result The config result code
 * @return Pointer to a static error message string
 */
const char *config_get_error_message(config_result_t result);

/**
 * Create a backup of the current state before loading new configuration
 * @param data Pointer to the main data structure
 * @param backup_filename Filename for the backup (NULL for auto-generated)
 * @return CONFIG_SUCCESS on success, error code on failure
 */
config_result_t config_create_backup(struct data *data, const char *backup_filename);

/**
 * Reset configuration to default values
 * @param data Pointer to the main data structure
 */
void config_reset_to_defaults(struct data *data);

/* Utility functions for getting configuration directory */

/**
 * Get the default configuration directory path
 * Creates the directory if it doesn't exist
 * @param buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true on success, false on failure
 */
bool config_get_config_dir(char *buffer, size_t buffer_size);

/**
 * Generate a timestamped backup filename
 * @param buffer Buffer to store the filename
 * @param buffer_size Size of the buffer
 * @return true on success, false on failure
 */
bool config_generate_backup_filename(char *buffer, size_t buffer_size);

/* Audio file loading functions */

/**
 * Load an audio file into a memory loop buffer
 * @param loop Pointer to the memory loop structure
 * @param filename Path to the audio file to load
 * @param sample_rate System sample rate for validation
 * @return true on success, false on failure
 */
bool load_audio_file_into_loop(struct memory_loop *loop, const char *filename, uint32_t sample_rate);

/**
 * Load all audio files referenced in the configuration
 * This should be called after config_load_state to actually load the audio data
 * @param data Pointer to the main data structure
 * @return Number of files successfully loaded, or -1 on error
 */
int config_load_audio_files(struct data *data);

#endif /* CONFIG_H */
