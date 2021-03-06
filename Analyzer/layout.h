/* Analyzes video data to determine the locations of markers.
 * The markers are detected using the following logic:
 * - Marker colors are always saturated RGB on every frame.
 * - One marker always has the same color over its whole area.
 * - Each marker changes color atleast once during the video.
 * - When the pixel changes color, the change should be large and sudden.
 */

#ifndef _TVG_LAYOUT_DETECTOR_H_
#define _TVG_LAYOUT_DETECTOR_H_

#include <stdint.h>
#include <stddef.h>
#include <glib.h>
#include <stdbool.h>

#define TVG_COLOR_THRESHOLD 200

typedef struct _layout_t layout_t;

typedef struct _marker_t {
  int x1;
  int y1;
  int x2;
  int y2;
  bool is_rgb;
  uint32_t crc;
} marker_t;

/* Create a new context for the layout detector */
layout_t *layout_create(int width, int height);

/* Release all resources associated with the context */
void layout_free(layout_t *layout);

/* Feed a new video frame to the layout detector (assumes RGB32 format) */
void layout_process(layout_t *layout, const uint8_t *frame, int stride);

/* Fetch a list of the detected marker locations
 * Returns array of marker_t structures. */
GArray* layout_fetch(layout_t *layout);

/* Fetch coordinates of the most changing pixel in the video */
void layout_most_changing_pixel(layout_t *layout, int *x, int *y);

/* Sample color value around a given pixel */
char* layout_sample_color(const uint8_t *frame, int stride, int x, int y);

/* Collect the current marker states in the video frame.
 * Returns a string of 'wrgbcmyk' characters, one for each marker. */
char* layout_read_markers(GArray* markers, const uint8_t *frame, int stride);

#endif
