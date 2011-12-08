/*
 * OptoFidelity Test Video Generator
 * Copyright (C) 2011 OptoFidelity <info@optofidelity.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-oftvg
 *
 * OFTVG (OptoFidelity Test Video Generator) is a filter that adds overlay
 * frame id markings to each frame.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! oftvg ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define DO_TIMING 1

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstoftvg.hh"
#include "timemeasure.h"

GST_DEBUG_CATEGORY_STATIC (gst_oftvg_debug);
#define GST_CAT_DEFAULT gst_oftvg_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
};

/* the capabilities of the inputs and outputs.
 *
 */
static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(
    GST_VIDEO_CAPS_YUV("I420") ";" GST_VIDEO_CAPS_YUV("YV12")
  )
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("ANY")
);

/* debug category for fltering log messages
 *
 */
#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_oftvg_debug, "oftvg", 0, "OptoFidelity Test Video Generator");

GST_BOILERPLATE_FULL (GstOFTVG, gst_oftvg, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_oftvg_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_oftvg_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_oftvg_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);

static gboolean gst_oftvg_set_caps(GstBaseTransform* btrans, GstCaps* incaps, GstCaps* outcaps);

static gboolean gst_oftvg_set_process_function(GstOFTVG* filter);

/* GObject vmethod implementations */

static void
gst_oftvg_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
    "OFTVG",
    "Filter/Editor/Video",
    "Overlays buffer timestamps on a video stream",
    "OptoFidelity <info@optofidelity.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
}

static guint32 gst_oftvg_get_frame_number(GstOFTVG* filter)
{
  // TODO: implement really
  static int frame_count = 0;
  return frame_count++;
}

/* initialize the oftvg's class */
static void
gst_oftvg_class_init (GstOFTVGClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass* btrans = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_oftvg_set_property;
  gobject_class->get_property = gst_oftvg_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
    g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE)));

  btrans->transform_ip = GST_DEBUG_FUNCPTR(gst_oftvg_transform_ip);
  btrans->set_caps     = GST_DEBUG_FUNCPTR(gst_oftvg_set_caps);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_oftvg_init (GstOFTVG *filter, GstOFTVGClass * klass)
{
  filter->silent = FALSE;

  if (!filter->silent)
  {
    g_print("GstOFTVG initialized.\n");
  }
}

static void
gst_oftvg_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOFTVG *filter = GST_OFTVG (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oftvg_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOFTVG *filter = GST_OFTVG (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

/* this function does the actual processing. In place processing.
 */
static GstFlowReturn
gst_oftvg_transform_ip(GstBaseTransform * base, GstBuffer *buf)
{
  GstOFTVG *filter = GST_OFTVG (base);
  int width;
  int height;

  //static int debug_counter = 0;
  if (filter->layout.length() == 0 /*|| debug_counter++%200==0*/)
  {
    filter->layout.clear();

    const gchar* filename = "../layout/test-layout-1920x1080-c.bmp";
      //"../layout/test-layout-1920x1080-b.png";
    int width = 1920;
    int height = 1080;
    GError* error = NULL;
    gst_oftvg_load_layout_bitmap(filename, &error, &filter->layout, width, height);

    if (error != NULL)
    {
      GST_ELEMENT_ERROR(filter, RESOURCE, OPEN_READ, (NULL), ("Could not open layout file: %s. %s", filename, error->message));
      return GST_FLOW_ERROR;
    }
  }

  width = filter->width;
  height = filter->height;
  filter->process_inplace(GST_BUFFER_DATA(buf), filter);

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
oftvg_init (GstPlugin * oftvg)
{
  return gst_element_register (oftvg, "oftvg", GST_RANK_NONE, GST_TYPE_OFTVG);
}

static void gst_oftvg_init_params(GstOFTVG* filter)
{
  if (!filter->silent)
  {
    g_print("gst_oftvg_init_params()\n");
  }

  filter->bit_on_color[0] = 255;
  filter->bit_on_color[1] = 128;
  filter->bit_on_color[2] = 128;
  filter->bit_on_color[3] = 0;

  filter->bit_off_color[0] = 0;
  filter->bit_off_color[1] = 128;
  filter->bit_off_color[2] = 128;
  filter->bit_off_color[3] = 0;
}

gboolean gst_oftvg_set_caps(GstBaseTransform* object, GstCaps* incaps, GstCaps* outcaps)
{
    GstOFTVG *filter = GST_OFTVG(object);

    if (!gst_video_format_parse_caps(incaps, &filter->in_format, &filter->width, &filter->height)
      ||
        !gst_video_format_parse_caps(incaps, &filter->out_format, &filter->width, &filter->height))
    {
      GST_WARNING_OBJECT(filter, "Failed to parse caps %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
        incaps, outcaps);
      return FALSE;
    }

    if (!gst_oftvg_set_process_function(filter))
    {
      GST_WARNING_OBJECT(filter, "No processing function for this caps");
      return FALSE;
    }

    gst_oftvg_init_params(filter);

    return TRUE;
}

void gst_oftvg_process_planar_yuv(guint8 *buf, GstOFTVG* filter)
{
  const guint8* const bit_on_color  = filter->bit_on_color;
  const guint8* const bit_off_color = filter->bit_off_color;
  const int width = filter->width;
  const int height = filter->height;
  int v_subs;
  int h_subs;

  timemeasure_t timer1 = begin_timing();

  const int y_stride  = gst_video_format_get_row_stride(filter->in_format, 0, width);
  const int uv_stride = gst_video_format_get_row_stride(filter->in_format, 1, width);
  g_assert(uv_stride == gst_video_format_get_row_stride(filter->in_format, 2, width));

  guint8* const bufY =
    buf + gst_video_format_get_component_offset(filter->in_format, 0, width, 
    height);
  guint8* const bufU =
    buf + gst_video_format_get_component_offset(filter->in_format, 1, width,
    height);
  guint8* const bufV =
    buf + gst_video_format_get_component_offset(filter->in_format, 2, width,
    height);

  switch (filter->in_format)
  {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      v_subs = h_subs = 1; // lg2(2)
      break;
    default:
      g_assert_not_reached();
      break;
  }

  const int frame_number = gst_oftvg_get_frame_number(filter);

  if (filter->layout.length() != 0)
  {
    int length = filter->layout.length();
    for (int i = 0; i < length; ++i)
    {
      const GstOFTVGElement& element = filter->layout.elements()[i];

      guint8* posY = bufY + element.y * y_stride;
      guint8* posU = bufU + (element.y >> v_subs) * uv_stride;
      guint8* posV = bufV + (element.y >> v_subs) * uv_stride;
      for (int dy = 0; dy < element.height; dy++)
      {
        gboolean bit_on = (((frame_number << 1) >> element.frameid_n) & 1) != 0;
        const guint8* color = bit_on ? bit_on_color : bit_off_color;
        for (int dx = element.x; dx < element.x + element.width; dx++)
        {
          posY[dx] = color[0];
          posU[dx>>h_subs] = color[1];
          posV[dx>>h_subs] = color[2];
        }
        posY += y_stride;
        if ((dy) & (1 << v_subs) == (1 << v_subs) - 1)
        {
          posU += uv_stride;
          posV += uv_stride;
        }
      }
    }
  }

  if (filter->silent == FALSE)
  {
    end_timing(timer1, "gst_oftvg_process_planar_yuv");
  }
}

static gboolean gst_oftvg_set_process_function(GstOFTVG* filter)
{
  filter->process_inplace = gst_oftvg_process_planar_yuv;
  return filter->process_inplace != NULL;
}

#ifndef PACKAGE
#define PACKAGE "gstoftvg"
#endif

/* gstreamer looks for this structure to register oftvgs
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "oftvg",
    "OptoFidelity Test Video Generator",
    oftvg_init,
    "Compiled " __DATE__ " " __TIME__,
    "LGPL",
    "OptoFidelity",
    "http://optofidelity.com/"
)