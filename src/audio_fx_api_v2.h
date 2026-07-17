#ifndef DISTROY_AUDIO_FX_API_V2_H
#define DISTROY_AUDIO_FX_API_V2_H

/* Transcribed from charlesvestal/schwung's docs/MODULES.md (no public
 * header file found for this ABI -- see README's "Known issue" section
 * for the open investigation into whether this transcription is exactly
 * right, carried over from the EMAX_FX project where on_midi() never
 * fired despite matching Ducker's module.json schema). */

#include <stdint.h>

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
    int (*get_clock_status)(void);
} host_api_v1_t;

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source); /* unused by DISTROY -- no MIDI dependency */
} audio_fx_api_v2_t;

audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host);

#endif /* DISTROY_AUDIO_FX_API_V2_H */
