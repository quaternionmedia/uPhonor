#include "uphonor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(const char *program_name)
{
  printf("Usage: %s <audiofile> [options]\n", program_name);
  printf("Options:\n");
  printf("  --pitch <semitones>    Set pitch shift in semitones (-12 to +12)\n");
  printf("  --speed <ratio>        Set playback speed (0.1 to 10.0)\n");
  printf("                         (automatically enables rubberband to preserve pitch)\n");
  printf("  --rubberband           Force enable rubberband processing\n");
  printf("  --no-rubberband        Disable rubberband (old-style speed/pitch coupling)\n");
  printf("  --help                 Show this help message\n");
  printf("\nMIDI Control:\n");
  printf("  CC 74                  Real-time playback speed control (0.25x - 4.0x)\n");
  printf("  CC 75                  Real-time pitch shift control (-12 to +12 semitones)\n");
  printf("  Note: Rubberband is auto-enabled when MIDI controls are used\n");
  printf("\nExamples:\n");
  printf("  %s myfile.wav --pitch 3 --rubberband\n", program_name);
  printf("  %s myfile.wav --speed 1.5 --pitch -2\n", program_name);
  printf("  %s myfile.wav --speed 2.0 --no-rubberband\n", program_name);
}

int parse_rubberband_args(int argc, char **argv, struct data *data)
{
  for (int i = 2; i < argc; i++)
  {
    if (strcmp(argv[i], "--pitch") == 0)
    {
      if (i + 1 >= argc)
      {
        fprintf(stderr, "Error: --pitch requires a value\n");
        return -1;
      }
      float pitch = atof(argv[++i]);
      if (pitch < -12.0f || pitch > 12.0f)
      {
        fprintf(stderr, "Error: pitch must be between -12 and +12 semitones\n");
        return -1;
      }
      set_pitch_shift(data, pitch);

      /* Automatically enable rubberband when pitch != 0.0 */
      if (pitch != 0.0f)
      {
        set_rubberband_enabled(data, true);
        printf("Set pitch shift to %.2f semitones (rubberband auto-enabled)\n", pitch);
      }
      else
      {
        printf("Set pitch shift to %.2f semitones\n", pitch);
      }
    }
    else if (strcmp(argv[i], "--speed") == 0)
    {
      if (i + 1 >= argc)
      {
        fprintf(stderr, "Error: --speed requires a value\n");
        return -1;
      }
      float speed = atof(argv[++i]);
      if (speed < 0.1f || speed > 10.0f)
      {
        fprintf(stderr, "Error: speed must be between 0.1 and 10.0\n");
        return -1;
      }
      set_playback_speed(data, speed);

      /* Automatically enable rubberband when speed != 1.0 to preserve pitch */
      if (speed != 1.0f)
      {
        set_rubberband_enabled(data, true);
        printf("Set playback speed to %.2f (rubberband auto-enabled to preserve pitch)\n", speed);
      }
      else
      {
        printf("Set playback speed to %.2f\n", speed);
      }
    }
    else if (strcmp(argv[i], "--rubberband") == 0)
    {
      set_rubberband_enabled(data, true);
      printf("Enabled rubberband processing\n");
    }
    else if (strcmp(argv[i], "--no-rubberband") == 0)
    {
      set_rubberband_enabled(data, false);
      printf("Disabled rubberband processing (old-style speed/pitch coupling)\n");
    }
    else if (strcmp(argv[i], "--volume") == 0)
    {
      if (i + 1 >= argc)
      {
        fprintf(stderr, "Error: --volume requires a value\n");
        return -1;
      }
      float volume = atof(argv[++i]);
      if (volume < 0.0f || volume > 1.0f)
      {
        fprintf(stderr, "Error: volume must be between 0.0 and 1.0\n");
        return -1;
      }
      set_volume(data, volume);
      printf("Set volume to %.2f\n", volume);
    }
    else if (strcmp(argv[i], "--help") == 0)
    {
      print_usage(argv[0]);
      return 1;
    }
    else
    {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return -1;
    }
  }
  return 0;
}

/* Enhanced CLI with rubberband controls */
int cli(int argc, char **argv, struct data *data)
{
  if (argc > 1 && strcmp(argv[1], "--help") == 0)
  {
    print_usage(argv[0]);
    return 1;
  }

  pw_log_info("Command line interface initialized with %d arguments", argc);

  if (argc == 1)
  {
    printf("uPhonor - Enhanced with Rubberband time-stretching and pitch-shifting\n");
    printf("No audio file specified. Starting in idle mode.\n");
    return 0;
  }

  /* Default values */
  data->volume = 1.0f;
  data->playback_speed = 1.0f;
  data->pitch_shift = 0.0f;
  data->rubberband_enabled = true;

  /* Parse rubberband-specific arguments */
  int parse_result = parse_rubberband_args(argc, argv, data);
  if (parse_result != 0)
  {
    return parse_result;
  }

  /* Start playing the file */
  if (argc > 1)
  {
    printf("Loading audio file: %s\n", argv[1]);
    if (data->rubberband_enabled)
    {
      printf("Rubberband processing enabled\n");
      printf("  Pitch shift: %.2f semitones\n", data->pitch_shift);
      printf("  Playback speed: %.2f\n", data->playback_speed);
      printf("  Volume: %.2f\n", data->volume);
    }
    else
    {
      printf("Using simple variable speed playback (speed: %.2f)\n", data->playback_speed);
    }
    return start_playing(data, argv[1]);
  }

  return 0;
}
