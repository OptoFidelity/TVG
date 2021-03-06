/* Handles the GStreamer code for creating a pipeline, opening
 * file and retrieving audio/video data. */

#ifndef _TVG_LOADER_H_
#define _TVG_LOADER_H_

#include <gst/gst.h>
#include <stdbool.h>

typedef struct _loader_t loader_t;

/* Load the given video file and setup a pipeline to parse it. */
loader_t *loader_open(const gchar *filename, GError **error);

/* Release all resources associated to the state. */
void loader_close(loader_t *state);

/* Get name of the video demuxer element */
const gchar *loader_get_demux(loader_t *state);

/* Get name of the video decompressor element */
const gchar *loader_get_video_decoder(loader_t *state);

/* Get name of the audio decompressor element */
const gchar *loader_get_audio_decoder(loader_t *state);

/* Get name of a muxer that would produce the same format. */
const gchar *loader_get_mux(loader_t *state);

/* Get name of a video encoder that would produce the same format. */
const gchar *loader_get_video_encoder(loader_t *state);

/* Get name of a audio encoder that would produce the same format. */
const gchar *loader_get_audio_encoder(loader_t *state);

/* Get resolution of video frames */
void loader_get_resolution(loader_t *state, int *width, int *height, int *stride);

/* Get samplerate of audio */
int loader_get_samplerate(loader_t *state);

/* Get framerate of video, or 0 for variable fps */
float loader_get_framerate(loader_t *state);

/* Retrieve video/audio buffers. Atleast one of the returned pointers
 * is non-NULL after the call, *except* on end-of-stream when it returns false. */
bool loader_get_buffer(loader_t *state, GstSample **audio_sample,
                       GstSample **video_sample, GError **error);


#endif
