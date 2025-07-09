#include "uphonor.h"
#include <string.h>

/* Print usage information */
static void print_usage(const char *program_name)
{
  printf("Usage: %s [OPTIONS] [FILE] [VOLUME]\n", program_name);
  printf("\nBasic usage:\n");
  printf("  %s                     - Start with default settings\n", program_name);
  printf("  %s file.wav            - Start and play specified file\n", program_name);
  printf("  %s file.wav 0.8        - Start, play file with volume 0.8\n", program_name);
  printf("\nConfiguration commands:\n");
  printf("  %s --save [session]    - Save current state to session file\n", program_name);
  printf("  %s --load [session]    - Load state from session file\n", program_name);
  printf("  %s --save-active [name] - Save only active loops\n", program_name);
  printf("  %s --list-sessions     - List available session files\n", program_name);
  printf("  %s --reset             - Reset to default settings\n", program_name);
  printf("  %s --status            - Show current configuration status\n", program_name);
  printf("  %s --help              - Show this help message\n", program_name);
  printf("\nExamples:\n");
  printf("  %s --save mysession    - Save current state as 'mysession.json'\n", program_name);
  printf("  %s --load mysession    - Load state from 'mysession.json'\n", program_name);
  printf("  %s --save-active jam   - Save active loops as 'jam_active.json'\n", program_name);
  printf("\n");
}

/* Parse the command line arguments

If there are no arguments, we just start the program
   If there is one argument, it is the file to play and loop
   If there are two arguments, the second is the volume level.

Configuration commands are handled separately and may exit the program. */

int cli(int argc, char **argv, struct data *data)
{
  pw_log_info("Command line interface initialized with %d arguments", argc);

  /* Handle configuration commands first */
  if (argc >= 2)
  {
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
    {
      print_usage(argv[0]);
      return 1; /* Exit after showing help */
    }

    else if (strcmp(argv[1], "--save") == 0)
    {
      const char *session_name = (argc >= 3) ? argv[2] : NULL;
      return save_current_session(data, session_name);
    }

    else if (strcmp(argv[1], "--load") == 0)
    {
      const char *session_name = (argc >= 3) ? argv[2] : NULL;
      return load_session(data, session_name);
    }

    else if (strcmp(argv[1], "--save-active") == 0)
    {
      const char *session_name = (argc >= 3) ? argv[2] : NULL;
      return save_active_loops(data, session_name);
    }

    else if (strcmp(argv[1], "--list-sessions") == 0)
    {
      list_available_sessions();
      return 1; /* Exit after listing */
    }

    else if (strcmp(argv[1], "--reset") == 0)
    {
      return reset_to_defaults(data);
    }

    else if (strcmp(argv[1], "--status") == 0)
    {
      print_config_status(data);
      return 1; /* Exit after showing status */
    }
  }

  /* Handle audio file and volume arguments */

  /* If a second argument is given, it is the volume level (when not using config commands). */
  if (argc == 3 && argv[1][0] != '-')
  {
    if (sscanf(argv[2], "%f", &data->volume) != 1)
    {
      fprintf(stderr, "invalid volume level: %s\n", argv[2]);
      return 1;
    }
    pw_log_info("Setting volume to %.2f", data->volume);
  }
  else
  {
    /* Default volume level is 1.0 */
    data->volume = 1.0f;
  }

  if (argc > 1 && argv[1][0] != '-')
  {
    // If there are any non-option arguments, the first is the file to play
    return start_playing(data, argv[1]);
  }

  // No audio file arguments, just start the program
  return 0;
}
