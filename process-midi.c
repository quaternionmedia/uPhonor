#include "uphonor.h"

#define PERIOD_NSEC (SPA_NSEC_PER_SEC / 8)

void process_midi(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct port *midi_output = data->midi_out;
  struct pw_buffer *buf;
  struct spa_data *d;
  struct spa_pod_builder builder;
  struct spa_pod_frame frame;
  uint64_t sample_offset, sample_period, sample_position, cycle;

  /*
   * Use the clock sample position.
   *
   * If the playback switches to using a different clock, we reset
   * playback as the sample position can then be discontinuous.
   */
  pw_log_debug("on_process: clock id %u, position %" PRIu64 ", duration %" PRIu64,
               position->clock.id, position->clock.position, position->clock.duration);
  if (data->clock_id != position->clock.id)
  {
    pw_log_info("switch to clock %u", position->clock.id);
    data->offset = position->clock.position - data->position;
    data->clock_id = position->clock.id;
  }

  sample_position = position->clock.position - data->offset;
  data->position = sample_position + position->clock.duration;

  /*
   * Produce note on/off every `PERIOD_NSEC` nanoseconds (rounded down to
   * samples, for simplicity).
   *
   * We want to place the notes on the playback timeline, so we use sample
   * positions (not real time!).
   */

  sample_period = PERIOD_NSEC * position->clock.rate.denom / position->clock.rate.num / SPA_NSEC_PER_SEC;

  cycle = sample_position / sample_period;
  if (sample_position % sample_period != 0)
    ++cycle;

  sample_offset = cycle * sample_period - sample_position;

  if (sample_offset >= position->clock.duration)
    return; /* don't need to produce anything yet */

  /* Get output buffer */
  if ((buf = pw_filter_dequeue_buffer(midi_output)) == NULL)
    return;

  /* Midi buffers always have exactly one data block */
  spa_assert(buf->buffer->n_datas == 1);

  d = &buf->buffer->datas[0];
  d->chunk->offset = 0;
  d->chunk->size = 0;
  d->chunk->stride = 1;
  d->chunk->flags = 0;

  /*
   * MIDI buffers contain a SPA POD with a sequence of
   * control messages and their raw MIDI data.
   */
  spa_pod_builder_init(&builder, d->data, d->maxsize);
  spa_pod_builder_push_sequence(&builder, &frame, 0);

  while (sample_offset < position->clock.duration)
  {
    if (cycle % 2 == 0)
    {
      /* MIDI note on, channel 0, middle C, max velocity */
      uint32_t event = 0x20903c7f;

      /* The time position of the message in the graph cycle
       * is given as offset from the cycle start, in
       * samples. The cycle has duration of `clock.duration`
       * samples, and the sample offset should satisfy
       * 0 <= sample_offset < position->clock.duration.
       */
      spa_pod_builder_control(&builder, sample_offset, SPA_CONTROL_UMP);

      /* Raw MIDI data for the message */
      spa_pod_builder_bytes(&builder, &event, sizeof(event));

      pw_log_info("note on at %" PRIu64, sample_position + sample_offset);
    }
    else
    {
      /* MIDI note off, channel 0, middle C, max velocity */
      uint32_t event = 0x20803c7f;

      spa_pod_builder_control(&builder, sample_offset, SPA_CONTROL_UMP);
      spa_pod_builder_bytes(&builder, &event, sizeof(event));

      pw_log_info("note off at %" PRIu64, sample_position + sample_offset);
    }

    sample_offset += sample_period;
    ++cycle;
  }

  /*
   * Finish the sequence and queue buffer to output.
   */
  spa_pod_builder_pop(&builder, &frame);
  d->chunk->size = builder.state.offset;

  pw_log_trace("produced %u/%u bytes", d->chunk->size, d->maxsize);

  pw_filter_queue_buffer(midi_output, buf);
}

static void state_changed(void *userdata, enum pw_filter_state old,
                          enum pw_filter_state state, const char *error)
{
  struct data *data = userdata;

  switch (state)
  {
  case PW_FILTER_STATE_STREAMING:
    /* reset playback position */
    pw_log_info("start playback");
    data->clock_id = SPA_ID_INVALID;
    data->offset = 0;
    data->position = 0;
    break;
  default:
    break;
  }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .process = process_midi,
    .state_changed = state_changed,
};
