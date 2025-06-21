#include "uphonor.h"

int cli(int argc, char **argv, struct data *data)
{
  /* A single argument: path to the audio file. */
  if (argc < 2)
  {
    fprintf(stderr,
            "expected an argument: the file to open\n");
    return 1;
  }
  /* If a second argument is given, it is the volume level. */
  if (argc == 3)
  {
    if (sscanf(argv[2], "%f", &data->volume) != 1)
    {
      fprintf(stderr, "invalid volume level: %s\n", argv[2]);
      return 1;
    }
  }
  else
  {
    /* Default volume level is 1.0 */
    data->volume = 1.0f;
  }

  /* We initialise libsndfile, the library we'll use to convert
     the audio file's content into interlaced float samples. */
  memset(&data->fileinfo, 0, sizeof(data->fileinfo));
  data->file = sf_open(argv[1], SFM_READ, &data->fileinfo);
  if (data->file == NULL)
  {
    fprintf(stderr, "file opening error: %s\n",
            sf_strerror(NULL));
    return 1;
  }
  return 0;
}
