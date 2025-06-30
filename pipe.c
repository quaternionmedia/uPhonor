#include "uphonor.h"

static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param)
{
  struct data *data = userdata;

  if (param == NULL || id != SPA_PARAM_Format)
    return;

  if (spa_format_parse(param,
                       &data->format.media_type,
                       &data->format.media_subtype) < 0)
    return;

  if (data->format.media_type != SPA_MEDIA_TYPE_audio ||
      data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  if (spa_format_audio_raw_parse(param, &data->format.info.raw) < 0)
    return;

  pw_log_info("  rate:%d channels:%d\n",
              data->format.info.raw.rate, data->format.info.raw.channels);

  /* Initialize rubberband now that we have format information */
  if (!data->rubberband_state && data->format.info.raw.rate > 0) {
    if (init_rubberband(data) < 0) {
      pw_log_warn("Failed to initialize rubberband");
    } else {
      pw_log_info("Rubberband initialized successfully");
    }
  }
}