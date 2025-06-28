#include "rt_nonrt_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pipewire/pipewire.h>

/* Ring buffer utilities */
static inline uint32_t next_power_of_2(uint32_t v)
{
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

/* Audio ring buffer implementation */
int audio_ring_buffer_init(struct audio_ring_buffer *rb, uint32_t size)
{
  size = next_power_of_2(size); /* Ensure power of 2 for fast modulo */

  rb->data = calloc(size, sizeof(float));
  if (!rb->data)
  {
    return -1;
  }

  rb->size = size;
  rb->mask = size - 1;
  rb->write_idx = 0;
  rb->read_idx = 0;

  return 0;
}

void audio_ring_buffer_destroy(struct audio_ring_buffer *rb)
{
  if (rb->data)
  {
    free(rb->data);
    rb->data = NULL;
  }
  rb->size = 0;
  rb->mask = 0;
  rb->write_idx = 0;
  rb->read_idx = 0;
}

uint32_t audio_ring_buffer_write_space(const struct audio_ring_buffer *rb)
{
  uint32_t w = rb->write_idx;
  uint32_t r = rb->read_idx;
  return (r - w - 1) & rb->mask;
}

uint32_t audio_ring_buffer_read_space(const struct audio_ring_buffer *rb)
{
  uint32_t w = rb->write_idx;
  uint32_t r = rb->read_idx;
  return (w - r) & rb->mask;
}

uint32_t audio_ring_buffer_write(struct audio_ring_buffer *rb,
                                 const float *data,
                                 uint32_t samples)
{
  uint32_t to_write = samples;
  uint32_t w = rb->write_idx;
  uint32_t available = audio_ring_buffer_write_space(rb);

  if (to_write > available)
  {
    to_write = available; /* Clamp to available space */
  }

  if (to_write == 0)
  {
    return 0;
  }

  /* Write in two parts if wrapping around */
  uint32_t w_idx = w & rb->mask;
  uint32_t cnt1 = rb->size - w_idx;

  if (cnt1 >= to_write)
  {
    /* Single contiguous write */
    memcpy(&rb->data[w_idx], data, to_write * sizeof(float));
  }
  else
  {
    /* Two-part write */
    memcpy(&rb->data[w_idx], data, cnt1 * sizeof(float));
    memcpy(&rb->data[0], &data[cnt1], (to_write - cnt1) * sizeof(float));
  }

  /* Update write pointer atomically */
  rb->write_idx = (w + to_write) & rb->mask;

  return to_write;
}

uint32_t audio_ring_buffer_read(struct audio_ring_buffer *rb,
                                float *data,
                                uint32_t samples)
{
  uint32_t to_read = samples;
  uint32_t r = rb->read_idx;
  uint32_t available = audio_ring_buffer_read_space(rb);

  if (to_read > available)
  {
    to_read = available; /* Clamp to available data */
  }

  if (to_read == 0)
  {
    return 0;
  }

  /* Read in two parts if wrapping around */
  uint32_t r_idx = r & rb->mask;
  uint32_t cnt1 = rb->size - r_idx;

  if (cnt1 >= to_read)
  {
    /* Single contiguous read */
    memcpy(data, &rb->data[r_idx], to_read * sizeof(float));
  }
  else
  {
    /* Two-part read */
    memcpy(data, &rb->data[r_idx], cnt1 * sizeof(float));
    memcpy(&data[cnt1], &rb->data[0], (to_read - cnt1) * sizeof(float));
  }

  /* Update read pointer atomically */
  rb->read_idx = (r + to_read) & rb->mask;

  return to_read;
}

/* Message queue implementation */
int message_queue_init(struct message_queue *mq, uint32_t size)
{
  size = next_power_of_2(size); /* Ensure power of 2 */

  mq->messages = calloc(size, sizeof(struct rt_message));
  if (!mq->messages)
  {
    return -1;
  }

  mq->size = size;
  mq->mask = size - 1;
  mq->write_idx = 0;
  mq->read_idx = 0;

  return 0;
}

void message_queue_destroy(struct message_queue *mq)
{
  if (mq->messages)
  {
    free(mq->messages);
    mq->messages = NULL;
  }
  mq->size = 0;
  mq->mask = 0;
  mq->write_idx = 0;
  mq->read_idx = 0;
}

bool message_queue_push(struct message_queue *mq, const struct rt_message *msg)
{
  uint32_t w = mq->write_idx;
  uint32_t next_w = (w + 1) & mq->mask;

  /* Check if queue is full */
  if (next_w == mq->read_idx)
  {
    return false; /* Queue full */
  }

  /* Copy message */
  mq->messages[w & mq->mask] = *msg;

  /* Update write pointer */
  mq->write_idx = next_w;

  return true;
}

bool message_queue_pop(struct message_queue *mq, struct rt_message *msg)
{
  uint32_t r = mq->read_idx;

  /* Check if queue is empty */
  if (r == mq->write_idx)
  {
    return false; /* Queue empty */
  }

  /* Copy message */
  *msg = mq->messages[r & mq->mask];

  /* Update read pointer */
  mq->read_idx = (r + 1) & mq->mask;

  return true;
}

/* Non-RT worker thread */
void *nonrt_worker_thread(void *arg)
{
  struct nonrt_worker *worker = (struct nonrt_worker *)arg;
  struct rt_message msg;
  float *audio_buffer = NULL;
  uint32_t audio_buffer_size = 4096; /* Initial size */

  /* Allocate temporary audio buffer */
  audio_buffer = malloc(audio_buffer_size * sizeof(float));
  if (!audio_buffer)
  {
    fprintf(stderr, "Failed to allocate audio buffer in non-RT thread\n");
    return NULL;
  }

  while (worker->running)
  {
    /* Process messages from RT thread */
    while (message_queue_pop(worker->msg_queue, &msg))
    {
      switch (msg.type)
      {
      case RT_MSG_START_RECORDING:
        if (!worker->recording_active)
        {
          /* Set up recording file info */
          worker->record_fileinfo.samplerate = msg.data.recording.sample_rate;
          worker->record_fileinfo.channels = msg.data.recording.channels;
          worker->record_fileinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

          /* Generate filename if needed */
          if (strlen(msg.data.recording.filename) == 0)
          {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            strftime(worker->current_filename, sizeof(worker->current_filename),
                     "recordings/recording_%Y%m%d_%H%M%S.wav", tm_info);
          }
          else
          {
            snprintf(worker->current_filename, sizeof(worker->current_filename),
                     "recordings/%s", msg.data.recording.filename);
          }

          /* Open recording file */
          worker->record_file = sf_open(worker->current_filename,
                                        SFM_WRITE, &worker->record_fileinfo);
          if (worker->record_file)
          {
            worker->recording_active = true;
            worker->frames_written = 0;
            printf("Started recording to: %s\n", worker->current_filename);
          }
          else
          {
            fprintf(stderr, "Could not open recording file: %s\n",
                    sf_strerror(NULL));
          }
        }
        break;

      case RT_MSG_STOP_RECORDING:
        if (worker->recording_active)
        {
          sf_close(worker->record_file);
          worker->record_file = NULL;
          worker->recording_active = false;
          printf("Stopped recording: %s (%lu frames written)\n",
                 worker->current_filename, worker->frames_written);
        }
        break;

      case RT_MSG_AUDIO_LEVEL:
        /* Could be used for metering display */
        if (msg.data.audio_level.rms_level > 0.001f)
        {
          printf("Audio level: RMS = %f\n", msg.data.audio_level.rms_level);
        }
        break;

      case RT_MSG_ERROR:
        fprintf(stderr, "RT Error: %s\n", msg.data.error.message);
        break;

      case RT_MSG_QUIT:
        worker->running = false;
        break;
      }
    }

    /* Process audio data from ring buffer */
    if (worker->recording_active && worker->record_file)
    {
      uint32_t available = audio_ring_buffer_read_space(worker->audio_buffer);

      if (available > 0)
      {
        /* Ensure buffer is large enough */
        if (available > audio_buffer_size)
        {
          audio_buffer_size = available;
          audio_buffer = realloc(audio_buffer, audio_buffer_size * sizeof(float));
          if (!audio_buffer)
          {
            fprintf(stderr, "Failed to reallocate audio buffer\n");
            break;
          }
        }

        /* Read from ring buffer and write to file */
        uint32_t read = audio_ring_buffer_read(worker->audio_buffer,
                                               audio_buffer, available);

        sf_count_t written = sf_writef_float(worker->record_file,
                                             audio_buffer, read);

        if (written != read)
        {
          fprintf(stderr, "Audio write error: wrote %ld of %d frames\n",
                  written, read);
          worker->buffer_underruns++;
        }
        else
        {
          worker->frames_written += written;
        }

        /* Sync to disk periodically */
        if (worker->frames_written % 48000 == 0)
        { /* Every ~1 second at 48kHz */
          sf_write_sync(worker->record_file);
        }
      }
    }

    /* Small sleep to prevent busy-wait */
    // usleep(1000); /* 1ms */
  }

  /* Cleanup */
  if (worker->recording_active && worker->record_file)
  {
    sf_close(worker->record_file);
    worker->record_file = NULL;
    worker->recording_active = false;
  }

  if (audio_buffer)
  {
    free(audio_buffer);
  }

  return NULL;
}

/* Bridge initialization */
int rt_nonrt_bridge_init(struct rt_nonrt_bridge *bridge,
                         uint32_t audio_buffer_size,
                         uint32_t msg_queue_size)
{
  memset(bridge, 0, sizeof(*bridge));

  /* Initialize audio ring buffer */
  if (audio_ring_buffer_init(&bridge->audio_buffer, audio_buffer_size) < 0)
  {
    return -1;
  }

  /* Initialize message queue */
  if (message_queue_init(&bridge->msg_queue, msg_queue_size) < 0)
  {
    audio_ring_buffer_destroy(&bridge->audio_buffer);
    return -1;
  }

  /* Initialize worker thread data */
  bridge->worker.audio_buffer = &bridge->audio_buffer;
  bridge->worker.msg_queue = &bridge->msg_queue;
  bridge->worker.running = true;
  bridge->worker.recording_active = false;
  bridge->worker.frames_written = 0;
  bridge->worker.buffer_overruns = 0;
  bridge->worker.buffer_underruns = 0;

  /* Create worker thread */
  if (pthread_create(&bridge->worker.thread, NULL, nonrt_worker_thread, &bridge->worker) != 0)
  {
    audio_ring_buffer_destroy(&bridge->audio_buffer);
    message_queue_destroy(&bridge->msg_queue);
    return -1;
  }

  bridge->rt_recording_enabled = false;
  bridge->rt_sample_rate = 48000;
  bridge->rt_channels = 1;

  return 0;
}

void rt_nonrt_bridge_destroy(struct rt_nonrt_bridge *bridge)
{
  if (!bridge)
    return;

  /* Signal worker thread to quit */
  struct rt_message quit_msg = {.type = RT_MSG_QUIT};
  message_queue_push(&bridge->msg_queue, &quit_msg);

  bridge->worker.running = false;

  /* Wait for worker thread to finish */
  pthread_join(bridge->worker.thread, NULL);

  /* Cleanup */
  audio_ring_buffer_destroy(&bridge->audio_buffer);
  message_queue_destroy(&bridge->msg_queue);
}

void rt_nonrt_bridge_cleanup(struct rt_nonrt_bridge *bridge)
{
  if (!bridge)
  {
    return;
  }

  /* Stop the worker thread */
  bridge->worker.running = false;

  /* Send quit message to ensure thread wakes up */
  struct rt_message quit_msg = {
      .type = RT_MSG_QUIT};
  rt_bridge_send_message(bridge, &quit_msg);

  /* Wait for worker thread to finish */
  pthread_join(bridge->worker.thread, NULL);

  /* Close any open recording file */
  if (bridge->worker.record_file)
  {
    sf_close(bridge->worker.record_file);
    bridge->worker.record_file = NULL;
  }

  /* Free audio buffer */
  if (bridge->audio_buffer.data)
  {
    free(bridge->audio_buffer.data);
    bridge->audio_buffer.data = NULL;
  }

  /* Free message queue */
  if (bridge->msg_queue.messages)
  {
    free(bridge->msg_queue.messages);
    bridge->msg_queue.messages = NULL;
  }
}

/* RT-safe functions */
bool rt_bridge_push_audio(struct rt_nonrt_bridge *bridge,
                          const float *samples,
                          uint32_t n_samples)
{
  if (!bridge->rt_recording_enabled)
  {
    return true; /* Not recording, no error */
  }

  uint32_t written = audio_ring_buffer_write(&bridge->audio_buffer, samples, n_samples);

  if (written < n_samples)
  {
    /* Buffer overrun detected - could increment a counter here */
    bridge->worker.buffer_overruns++;
    return false;
  }

  return true;
}

bool rt_bridge_send_message(struct rt_nonrt_bridge *bridge,
                            const struct rt_message *msg)
{
  return message_queue_push(&bridge->msg_queue, msg);
}

void rt_bridge_set_recording_enabled(struct rt_nonrt_bridge *bridge, bool enabled)
{
  bridge->rt_recording_enabled = enabled;
}

bool rt_bridge_is_recording_enabled(struct rt_nonrt_bridge *bridge)
{
  return bridge->rt_recording_enabled;
}

/* Non-RT safe utility functions */
const char *rt_bridge_get_current_filename(struct rt_nonrt_bridge *bridge)
{
  if (!bridge || !bridge->worker.current_filename[0])
  {
    return NULL;
  }
  return bridge->worker.current_filename;
}

bool rt_bridge_is_recording_active(struct rt_nonrt_bridge *bridge)
{
  if (!bridge)
  {
    return false;
  }
  return bridge->worker.recording_active;
}
