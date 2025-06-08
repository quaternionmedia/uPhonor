#include "uphonor.h"

/* on_process is responsible for generating the audio samples when
   the stream should be outputting audio. It might not get called,
   if the ports of the stream are not connected using links to
   input ports.

   The general process is the following:
     - pw_stream_dequeue_buffer() to retrieve a buffer from the
       buffer queue;
     - fill the buffer with data and set its properties
       (offset, stride and size);
     - pw_stream_queue_buffer() to hand the buffer back to
       PipeWire.

   We'll use the following calling convention: a frame is composed
   of multiple samples, one per channel. */
void on_process(void *userdata)
{
  /* Retrieve our global data structure. */
  struct data *data = userdata;
  struct pw_buffer *b;

  // Check if MIDI input requested a reset
  if (data->reset_audio)
  {
    if (sf_seek(data->file, 0, SEEK_SET) < 0)
    {
      fprintf(stderr, "file seek error during reset: %s\n",
              sf_strerror(data->file));
    }
    else
    {
      pw_log_info("Audio playback reset to beginning");
    }
    data->reset_audio = false; // Clear the flag
  }

  /* Dequeue the buffer which we will fill up with data. */
  if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL)
  {
    pw_log_warn("out of buffers: %m");
    return;
  }

  /* Retrieve buf, a pointer to the actual memory address at
     which we'll put our samples. */
  float *buf = b->buffer->datas[0].data;
  if (buf == NULL)
    return;

  /* stride is the size of one frame. */
  uint32_t stride = sizeof(float) * data->fileinfo.channels;
  /* n_frames is the number of frames we will output. We decide
     to output the maximum we can fit in the buffer we were
     given, or the requested amount if one was given. */
  uint32_t n_frames = b->buffer->datas[0].maxsize / stride;
  if (b->requested)
    n_frames = SPA_MIN(n_frames, b->requested);

  /* We can now fill the buffer! We keep reading from libsndfile
     until the buffer is full. */
  sf_count_t current = 0;
  while (current < n_frames)
  {
    sf_count_t ret = sf_readf_float(data->file,
                                    &buf[current * data->fileinfo.channels],
                                    n_frames - current);
    if (ret < 0)
    {
      fprintf(stderr, "file reading error: %s\n",
              sf_strerror(data->file));
      goto error_after_dequeue;
    }

    current += ret;

    // Apply volume control to the samples we just read
    for (sf_count_t i = (current - ret) * data->fileinfo.channels;
         i < current * data->fileinfo.channels; i++)
    {
      buf[i] *= data->volume;
    }

    /* If libsndfile did not manage to fill the buffer we asked
       it to fill, we assume we reached the end of the file
       (as described by libsndfile's documentation) and we
       seek back to the start. */
    if (current != n_frames &&
        sf_seek(data->file, 0, SEEK_SET) < 0)
    {
      fprintf(stderr, "file seek error: %s\n",
              sf_strerror(data->file));
      goto error_after_dequeue;
    }
  }

  /* We describe the buffer we just filled before handing it back
     to PipeWire.  */
  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = n_frames * stride;
  pw_stream_queue_buffer(data->stream, b);

  return;

error_after_dequeue:
  /* If an error occured after dequeuing a buffer, we end the
     event loop. The current buffer will be sent to the next
     node so we need to make it empty to avoid sending corrupted
     data. */

  pw_main_loop_quit(data->loop);
  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = 0;
  b->buffer->datas[0].chunk->size = 0;
  pw_stream_queue_buffer(data->stream, b);
}
