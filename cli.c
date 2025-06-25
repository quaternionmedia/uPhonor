#include "uphonor.h"

/* Parse the command line arguments

If there are no arguments, we just start the program
   If there is one argument, it is the file to play and loop
   If there are two arguments, the second is the volume level. */

int cli(int argc, char **argv, struct data *data)
{
  pw_log_info("Command line interface initialized with %d arguments", argc);
  /* If a second argument is given, it is the volume level. */
  if (argc == 3)
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
  if (argc > 1)
  {
    // If there are any arguments, the first is the file to play
    return start_playing(data, argv[1]);
  }

  // No arguments, just start the program
  return 0;
}
