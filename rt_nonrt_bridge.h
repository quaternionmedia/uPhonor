#ifndef RT_NONRT_BRIDGE_H
#define RT_NONRT_BRIDGE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sndfile.h>

/* Use volatile for basic thread safety - can be upgraded to atomics later */

/* Lock-free ring buffer for audio data */
struct audio_ring_buffer
{
  float *data;
  uint32_t size; /* Power of 2 size */
  uint32_t mask; /* size - 1 for fast modulo */
  volatile uint32_t write_idx;
  volatile uint32_t read_idx;
};

/* Message types for RT -> Non-RT communication */
enum rt_message_type
{
  RT_MSG_START_RECORDING,
  RT_MSG_STOP_RECORDING,
  RT_MSG_AUDIO_LEVEL,
  RT_MSG_ERROR,
  RT_MSG_QUIT
};

/* Message structure for RT -> Non-RT communication */
struct rt_message
{
  enum rt_message_type type;
  union
  {
    struct
    {
      char filename[256];
      uint32_t sample_rate;
      uint32_t channels;
    } recording;
    struct
    {
      float rms_level;
    } audio_level;
    struct
    {
      char message[256];
    } error;
  } data;
};

/* Lock-free message queue */
struct message_queue
{
  struct rt_message *messages;
  uint32_t size;
  uint32_t mask;
  volatile uint32_t write_idx;
  volatile uint32_t read_idx;
};

/* Non-RT worker thread data */
struct nonrt_worker
{
  pthread_t thread;
  struct audio_ring_buffer *audio_buffer;
  struct message_queue *msg_queue;
  volatile bool running;

  /* Recording state (managed by non-RT thread) */
  SNDFILE *record_file;
  SF_INFO record_fileinfo;
  bool recording_active;
  char current_filename[512];

  /* Performance monitoring */
  uint64_t frames_written;
  uint64_t buffer_overruns;
  uint64_t buffer_underruns;
};

/* Main bridge structure */
struct rt_nonrt_bridge
{
  struct audio_ring_buffer audio_buffer;
  struct message_queue msg_queue;
  struct nonrt_worker worker;

  /* RT thread state */
  volatile bool rt_recording_enabled;
  uint32_t rt_sample_rate;
  uint32_t rt_channels;
};

/* Function declarations */
int rt_nonrt_bridge_init(struct rt_nonrt_bridge *bridge,
                         uint32_t audio_buffer_size,
                         uint32_t msg_queue_size);

void rt_nonrt_bridge_destroy(struct rt_nonrt_bridge *bridge);

/* RT-safe functions (call from audio callback) */
bool rt_bridge_push_audio(struct rt_nonrt_bridge *bridge,
                          const float *samples,
                          uint32_t n_samples);

bool rt_bridge_send_message(struct rt_nonrt_bridge *bridge,
                            const struct rt_message *msg);

void rt_bridge_set_recording_enabled(struct rt_nonrt_bridge *bridge, bool enabled);
bool rt_bridge_is_recording_enabled(struct rt_nonrt_bridge *bridge);

/* Non-RT safe utility functions */
const char *rt_bridge_get_current_filename(struct rt_nonrt_bridge *bridge);
bool rt_bridge_is_recording_active(struct rt_nonrt_bridge *bridge);

/* Ring buffer operations (lock-free) */
int audio_ring_buffer_init(struct audio_ring_buffer *rb, uint32_t size);
void audio_ring_buffer_destroy(struct audio_ring_buffer *rb);
uint32_t audio_ring_buffer_write_space(const struct audio_ring_buffer *rb);
uint32_t audio_ring_buffer_read_space(const struct audio_ring_buffer *rb);
uint32_t audio_ring_buffer_write(struct audio_ring_buffer *rb,
                                 const float *data,
                                 uint32_t samples);
uint32_t audio_ring_buffer_read(struct audio_ring_buffer *rb,
                                float *data,
                                uint32_t samples);

/* Message queue operations (lock-free) */
int message_queue_init(struct message_queue *mq, uint32_t size);
void message_queue_destroy(struct message_queue *mq);
bool message_queue_push(struct message_queue *mq, const struct rt_message *msg);
bool message_queue_pop(struct message_queue *mq, struct rt_message *msg);

/* Non-RT worker thread */
void *nonrt_worker_thread(void *arg);

#endif /* RT_NONRT_BRIDGE_H */
