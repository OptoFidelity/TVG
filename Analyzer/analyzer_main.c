#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <gst/gst.h>
#include "loader.h"
#include "layout.h"
#include "lipsync.h"
#include "markertype.h"

GST_DEBUG_CATEGORY(tvg_analyzer_debug);
#define GST_CAT_DEFAULT tvg_analyzer_debug

typedef struct {
  const char *filename;
  GArray *markers; /* marker_t, detected markers */
  int num_frames;
  GArray *lipsync_markers; /* lipsync_marker_t, detected beeps */
  GArray *frame_data; /* char*, detected marker states in frames */
  GArray *frame_times; /* GstClockTime */
  int mc_x, mc_y; /* Coordinates for most changing pixel */
  GArray *frame_colors; /* char*, detected marker color for most changing pixel */
  GArray *warnings;
  int rgb6_marker_index; /* Index of the RGB6 marker */
  int samplerate; /* Audio samplerate */
} main_state_t;

/* First pass through the input video:
 * - Count number of frames
 * - Detect the location of markers in video
 */
bool first_pass(main_state_t *main_state, GError **error)
{
  int width, height, stride;
  layout_t *layout_state;
  loader_t *loader_state;
  float framerate;
  
  *error = NULL;
  loader_state = loader_open(main_state->filename, error);
  if (*error != NULL)
    return false;
  
  loader_get_resolution(loader_state, &width, &height, &stride);
  main_state->samplerate = loader_get_samplerate(loader_state);
  framerate = loader_get_framerate(loader_state);
  
  printf("{\n");
  printf("    \"file\":           \"%s\",\n", main_state->filename); 
  printf("    \"demuxer\":        \"%s\",\n", loader_get_demux(loader_state));
  printf("    \"video_decoder\":  \"%s\",\n", loader_get_video_decoder(loader_state));
  printf("    \"audio_decoder\":  \"%s\",\n", loader_get_audio_decoder(loader_state));
  printf("    \"muxer\":          \"%s\",\n", loader_get_mux(loader_state));
  printf("    \"video_encoder\":  \"%s\",\n", loader_get_video_encoder(loader_state));
  printf("    \"audio_encoder\":  \"%s\",\n", loader_get_audio_encoder(loader_state));
  printf("    \"resolution\":     [%d,%d],\n", width, height);
  
  if (framerate != 0)
    printf("    \"framerate\":    %8.2f,\n", framerate);
  else
    printf("    \"framerate\":    \"variable\",\n");
  
  printf("    \"audio_rate\":   %8d,\n", main_state->samplerate);
  
  layout_state = layout_create(width, height);
  
  int num_frames = 0;
  GstSample *audio_sample, *video_sample;
  GstClockTime video_start_time = GST_CLOCK_TIME_NONE, audio_start_time = GST_CLOCK_TIME_NONE;
  GstClockTime video_end_time = 0, audio_end_time = 0;
  while (loader_get_buffer(loader_state, &audio_sample, &video_sample, error))
  {
    if (video_sample != NULL)
    {
      num_frames++;
      
      GstBuffer *video_buf = gst_sample_get_buffer(video_sample);
      GstClockTime video_time = gst_segment_to_running_time(gst_sample_get_segment(video_sample),
                                                            GST_FORMAT_TIME,
                                                            GST_BUFFER_PTS(video_buf));
      
      if (isatty(1))
      {
        GST_INFO("Processing frame %d, time %" GST_TIME_FORMAT "\n",
                 num_frames, GST_TIME_ARGS(video_time));
        printf("[%5d]  \r", num_frames);
        fflush(stdout);
      }
      
      {
        GstMapInfo mapinfo;
        gst_buffer_map(video_buf, &mapinfo, GST_MAP_READ);
        layout_process(layout_state, mapinfo.data, stride);
        gst_buffer_unmap(video_buf, &mapinfo);
      }
      
      if (!GST_CLOCK_TIME_IS_VALID(video_start_time))
      {
        /* First frame */
        video_start_time = video_time;
      }
      else
      {
        GstClockTimeDiff delta = GST_CLOCK_DIFF(video_end_time, video_time);
        if (delta < -GST_MSECOND || delta > GST_MSECOND)
        {
          gchar* m = g_strdup_printf(
                 "Gap in video times (frame %d, offset = %0.3f s)",
                 num_frames, (float)delta / GST_SECOND);
          g_array_append_val(main_state->warnings, m);
        }
      }
      
      video_end_time = video_time + video_buf->duration;
      gst_sample_unref(video_sample);
    }
    
    if (audio_sample != NULL)
    {
      GstBuffer *audio_buf = gst_sample_get_buffer(audio_sample);
      GstClockTime audio_time = gst_segment_to_running_time(gst_sample_get_segment(audio_sample),
                                                            GST_FORMAT_TIME,
                                                            GST_BUFFER_PTS(audio_buf));
      
      
      if (!GST_CLOCK_TIME_IS_VALID(audio_start_time))
      {
        audio_start_time = audio_time;
      }
      else
      {
        GstClockTimeDiff delta = GST_CLOCK_DIFF(audio_end_time, audio_time);
        if (delta < -GST_MSECOND || delta > GST_MSECOND)
        {
          gchar* m = g_strdup_printf(
                 "Gap in audio times (at %0.3f s, offset = %0.3f s)",
                 (float)audio_end_time / GST_SECOND, (float)delta / GST_SECOND);
          g_array_append_val(main_state->warnings, m);
        }
      }
      
      audio_end_time = audio_time + audio_buf->duration;
      gst_buffer_unref(audio_buf);
    }
    
    if (*error != NULL)
      return false;
  }
  
  main_state->num_frames = num_frames;
  printf("    \"total_frames\": %8d,\n", num_frames);
  printf("    \"video_length\": %8.3f,\n", (float)video_end_time / GST_SECOND);
  printf("    \"audio_length\": %8.3f,\n", (float)audio_end_time / GST_SECOND);
  
  if (GST_CLOCK_TIME_IS_VALID(video_start_time) && GST_CLOCK_TIME_IS_VALID(audio_start_time))
  {
    if (abs(video_start_time - audio_start_time) > GST_MSECOND)
    {
      gchar* m = g_strdup_printf(
        "Audio and video start at different times (audio = %0.3f s, video = %0.3f s)",
        (float)audio_start_time / GST_SECOND, (float)video_start_time / GST_SECOND);
      g_array_append_val(main_state->warnings, m);
    }
  }
  
  main_state->markers = layout_fetch(layout_state);
  layout_most_changing_pixel(layout_state, &main_state->mc_x, &main_state->mc_y);
  printf("    \"markers_found\":%8d,\n", main_state->markers->len);
  printf("    \"most_changing_pixel\": [%4d,%4d],\n", main_state->mc_x, main_state->mc_y);
  
  loader_close(loader_state);
  layout_free(layout_state);
  return true;
}

/* Second pass through the input video:
 * - Read the states of video markers
 * - Detect audio markers
 */
bool second_pass(main_state_t *main_state, GError **error)
{
  int width, height, stride;
  loader_t *loader_state;
  lipsync_t *lipsync_state;
  
  *error = NULL;
  loader_state = loader_open(main_state->filename, error);
  if (*error != NULL)
    return false;
  
  loader_get_resolution(loader_state, &width, &height, &stride);
  
  lipsync_state = lipsync_create(main_state->samplerate);
  main_state->frame_data = g_array_new(false, false, sizeof(char*));
  main_state->frame_times = g_array_new(false, false, sizeof(GstClockTime));
  main_state->frame_colors = g_array_new(false, false, sizeof(char*));
  
  int num_frames = 0;
  GstSample *audio_sample, *video_sample;
  while (loader_get_buffer(loader_state, &audio_sample, &video_sample, error))
  {
    if (video_sample != NULL)
    {
      num_frames++;
      
      if (isatty(1))
      {
        printf("[%5d/%5d]  \r", num_frames, main_state->num_frames);
        fflush(stdout);
      }
      
      GstBuffer *video_buf = gst_sample_get_buffer(video_sample);
      GstClockTime video_time = gst_segment_to_running_time(gst_sample_get_segment(video_sample),
                                                            GST_FORMAT_TIME,
                                                            GST_BUFFER_PTS(video_buf));
      
      {
        GstMapInfo mapinfo;
        gst_buffer_map(video_buf, &mapinfo, GST_MAP_READ);
        
        char *states = layout_read_markers(main_state->markers, mapinfo.data, stride);
        char *color = layout_sample_color(mapinfo.data, stride, main_state->mc_x, main_state->mc_y);
        g_array_append_val(main_state->frame_data, states);
        g_array_append_val(main_state->frame_times, video_time);
        g_array_append_val(main_state->frame_colors, color);
        
        gst_buffer_unmap(video_buf, &mapinfo);
      }
      
      gst_buffer_unref(video_buf);
    }
    
    if (audio_sample != NULL)
    {
      GstBuffer *audio_buf = gst_sample_get_buffer(audio_sample);
      GstClockTime audio_time = gst_segment_to_running_time(gst_sample_get_segment(audio_sample),
                                                            GST_FORMAT_TIME,
                                                            GST_BUFFER_PTS(audio_buf));
      
      {
        GstMapInfo mapinfo;
        gst_buffer_map(audio_buf, &mapinfo, GST_MAP_READ);
        
        lipsync_process(lipsync_state, audio_time, main_state->samplerate,
                        (const int16_t*)mapinfo.data, mapinfo.size / 2);
        
        gst_buffer_unmap(audio_buf, &mapinfo);
      }
      gst_buffer_unref(audio_buf);
    }
    
    if (*error != NULL)
      return false;
  }
  
  main_state->lipsync_markers = lipsync_fetch(lipsync_state);
  lipsync_free(lipsync_state);
  loader_close(loader_state);
  return true;
}

/* Print the collected information about markers */
static void print_marker_info(main_state_t *main_state)
{
  videoinfo_t *videoinfo = markertype_analyze(main_state->frame_data);
  size_t i;
  
  printf("    \"markers\": [\n");
  
  main_state->rgb6_marker_index = -1;
  for (i = 0; i < main_state->markers->len; i++)
  {
    marker_t *marker = &g_array_index(main_state->markers, marker_t, i);
    markerinfo_t *info = &videoinfo->markerinfo[i];
    
    printf("      {\"index\": %3d, \"pos\": [%4d,%4d], \"size\": [%3d,%3d], \"crc\": \"%08x\", ",
           (int)i, marker->x1, marker->y1,
           marker->x2 - marker->x1 + 1, marker->y2 - marker->y1 + 1,
           marker->crc
          );
    
    if (info->type == TVG_MARKER_SYNCMARK)
      printf("\"type\": \"SYNCMARK\", \"interval\": %d", info->interval);
    else if (info->type == TVG_MARKER_FRAMEID)
      printf("\"type\": \"FRAMEID\", \"interval\": %d", info->interval);
    else if (info->type == TVG_MARKER_RGB6)
      printf("\"type\": \"RGB6\"");
    else
      printf("\"type\": \"UNKNOWN\"");
    
    if (i == main_state->markers->len - 1)
      printf("}\n");
    else
      printf("},\n");
    
    if (info->type == TVG_MARKER_RGB6)
      main_state->rgb6_marker_index = i;
  }
  
  printf("    ],\n");
  
  printf("    \"video_structure\": {\n");
  printf("      \"header_frames\": %8d,\n", videoinfo->num_header_frames);
  printf("      \"locator_frames\":%8d,\n", videoinfo->num_locator_frames);
  printf("      \"content_frames\":%8d,\n", videoinfo->num_content_frames);
  printf("      \"trailer_frames\":%8d\n", videoinfo->num_trailer_frames);
  printf("    },\n");
  
  markertype_free(videoinfo);
}

/* Calculate statistics about lipsync markers */
static void print_lipsync_info(main_state_t *main_state)
{
  size_t beep_index = 0;
  size_t frame_index = 0;
  int video_markers = 0;
  float min_lipsync = 0;
  float max_lipsync = 0;
  for (frame_index = 0; frame_index < main_state->frame_data->len; frame_index++)
  {
    char c = g_array_index(main_state->frame_data, char*, frame_index)
              [main_state->rgb6_marker_index];
    if (c == 'k')
    {
      if (beep_index < main_state->lipsync_markers->len)
      {
        /* Lipsync frame, compare to matching lipsync beep */
        GstClockTimeDiff frame_time = g_array_index(main_state->frame_times,
                                                    GstClockTime, frame_index);
        lipsync_marker_t beep = g_array_index(main_state->lipsync_markers,
                                              lipsync_marker_t, beep_index++);
        
        float delta = (float)((GstClockTimeDiff)beep.start_time - frame_time) / GST_SECOND;
        
        if (video_markers == 0)
        {
          min_lipsync = delta;
          max_lipsync = delta;
        }
        else
        {
          if (delta < min_lipsync) min_lipsync = delta;
          if (delta > max_lipsync) max_lipsync = delta;
        }
      }
      
      video_markers++;
    }
  }
  
  printf("    \"lipsync\": {\n");
  printf("      \"audio_markers\":    %8d,\n", main_state->lipsync_markers->len);
  printf("      \"video_markers\":    %8d", video_markers);
  
  if (main_state->lipsync_markers->len > 0 && video_markers > 0)
  {
    printf(",\n");
    printf("      \"audio_delay_min_ms\": %6.1f,\n", 1000 * min_lipsync);
    printf("      \"audio_delay_max_ms\": %6.1f", 1000 * max_lipsync);
  }
  printf("\n    },\n");
}

static void save_details(main_state_t *main_state)
{
  size_t frame_index, lipsync_index = 0;
  FILE *f;
  
  f = fopen("frames.txt", "w");
  
  if (!f)
    return;
  
  for (frame_index = 0; frame_index <= main_state->frame_data->len; frame_index++)
  {
    GstClockTime frame_time = 0;
    char *frame_data = NULL;
    char *frame_color = NULL;
    
    /* On the last iteration, process all the beeps that come after the last video frame */
    bool last = (frame_index == main_state->frame_data->len);
    
    if (!last)
    {
      frame_time = g_array_index(main_state->frame_times, GstClockTime, frame_index);
      frame_data = g_array_index(main_state->frame_data, char*, frame_index);
      frame_color = g_array_index(main_state->frame_colors, char*, frame_index);
    }
    
    while (lipsync_index < main_state->lipsync_markers->len)
    {
      lipsync_marker_t beep = g_array_index(main_state->lipsync_markers, lipsync_marker_t, lipsync_index);
      GstClockTime beep_start = beep.start_time;
      GstClockTime beep_length = (beep.end_sample - beep.start_sample) * GST_SECOND / main_state->samplerate;
      
      if (last || beep_start <= frame_time)
      {
        fprintf(f, "AUDIO: %8d %5d\n",
                (int)(beep_start / GST_USECOND),
                (int)(beep_length / GST_USECOND)
              );
        lipsync_index++;
      }
      else
      {
        break;
      }
    }
    
    if (!last)
    {
      fprintf(f, "VIDEO: %8d %s %s\n", (int)(frame_time / GST_USECOND), frame_data, frame_color);
    }
  }
  
  fclose(f);
  
  printf("    \"frame_data\": \"frames.txt\",\n");
}

int main(int argc, char *argv[])
{
  size_t i;
  
  /* Initialize gstreamer. Will handle any gst-specific commandline options. */
  gst_init(&argc, &argv);
  GST_DEBUG_CATEGORY_INIT (tvg_analyzer_debug, "tvg_analyzer", 0, "OF TVG Video Analyzer");
  
  /* There should be only one remaining argument. */
  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s [gst options] <video file>\n", argv[0]);
    return 1;
  }
  
  {
    main_state_t main_state = {0};
    GError *error = NULL;
    
    main_state.filename = argv[1];
    main_state.warnings = g_array_new(false, false, sizeof(char*));
    
    if (!first_pass(&main_state, &error))
    {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
      return 2;
    }
    
    if (!second_pass(&main_state, &error))
    {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
      return 3;
    }
    
    print_marker_info(&main_state);
    print_lipsync_info(&main_state);
    save_details(&main_state);
    
    printf("    \"warnings\": [\n");
    for (i = 0; i < main_state.warnings->len; i++)
    {
      printf("      \"%s\"%s\n",
             g_array_index(main_state.warnings, char*, i),
             (i == main_state.warnings->len - 1) ? "" : ",");
    }
    printf("    ]\n");
    printf("}\n");
  }
  
  return 0;
}