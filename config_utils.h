#ifndef CONFIG_UTILS_H
#define CONFIG_UTILS_H

#include "uphonor.h"

/* High-level configuration management functions for easy integration */

/**
 * Save current session with user-friendly feedback
 * @param data Pointer to the main data structure
 * @param session_name Name of the session (NULL for default, .json extension optional)
 * @return 0 on success, -1 on failure
 */
int save_current_session(struct data *data, const char *session_name);

/**
 * Load session with user-friendly feedback and automatic backup
 * @param data Pointer to the main data structure
 * @param session_name Name of the session to load (NULL for default, .json extension optional)
 * @return 0 on success, -1 on failure
 */
int load_session(struct data *data, const char *session_name);

/**
 * Save only the loops that have recorded content
 * @param data Pointer to the main data structure
 * @param session_name Base name for the session (will add _active suffix)
 * @return 0 on success, -1 on failure
 */
int save_active_loops(struct data *data, const char *session_name);

/**
 * Reset all loops and settings to defaults with backup
 * @param data Pointer to the main data structure
 * @return 0 on success, -1 on failure
 */
int reset_to_defaults(struct data *data);

/**
 * List available configuration files
 */
void list_available_sessions(void);

/**
 * Print current configuration status to stdout
 * @param data Pointer to the main data structure
 */
void print_config_status(struct data *data);

#endif /* CONFIG_UTILS_H */
