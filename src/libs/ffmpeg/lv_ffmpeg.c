/**
 * @file lv_ffmpeg.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_ffmpeg.h"
#if LV_USE_FFMPEG != 0

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>

/*********************
 *      DEFINES
 *********************/
#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
    #define AV_PIX_FMT_TRUE_COLOR AV_PIX_FMT_RGB8
#elif LV_COLOR_DEPTH == 16
    #define AV_PIX_FMT_TRUE_COLOR AV_PIX_FMT_RGB565LE
#elif LV_COLOR_DEPTH == 32
    #define AV_PIX_FMT_TRUE_COLOR AV_PIX_FMT_BGR0
#else
    #error Unsupported  LV_COLOR_DEPTH
#endif

/**********************
 *      TYPEDEFS
 **********************/
struct ffmpeg_context_s {
    AVFormatContext * fmt_ctx;
    AVCodecContext * video_dec_ctx;
    AVStream * video_stream;
    uint8_t * video_src_data[4];
    uint8_t * video_dst_data[4];
    struct SwsContext * sws_ctx;
    AVFrame * frame;
    AVPacket * pkt;
    int video_stream_idx;
    int video_src_linesize[4];
    int video_dst_linesize[4];
    enum AVPixelFormat video_dst_pix_fmt;
    bool has_alpha;
    lv_frame_index_t last_rendered_frame;
};

#pragma pack(1)

struct lv_img_pixel_color_s {
    lv_color_t c;
    uint8_t alpha;
};

#pragma pack()

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_res_t decoder_accept(const lv_img_src_t * src, uint8_t * caps, void * user_data);
static lv_res_t decoder_open(lv_img_dec_dsc_t * dsc, const lv_img_dec_flags_t flags, void * user_data);
static void decoder_close(lv_img_dec_dsc_t * dsc, void * user_data);

static struct ffmpeg_context_s * ffmpeg_open_file(const char * path);
static void ffmpeg_close(struct ffmpeg_context_s * ffmpeg_ctx);
static void ffmpeg_close_src_ctx(struct ffmpeg_context_s * ffmpeg_ctx);
static void ffmpeg_close_dst_ctx(struct ffmpeg_context_s * ffmpeg_ctx);
static int ffmpeg_image_allocate(struct ffmpeg_context_s * ffmpeg_ctx);
static int ffmpeg_get_img_header(const char * path, lv_img_header_t * header);
static int ffmpeg_get_frame_refr_period(struct ffmpeg_context_s * ffmpeg_ctx);
static uint8_t * ffmpeg_get_img_data(struct ffmpeg_context_s * ffmpeg_ctx);
static int ffmpeg_update_next_frame(struct ffmpeg_context_s * ffmpeg_ctx);
static int ffmpeg_output_video_frame(struct ffmpeg_context_s * ffmpeg_ctx);
static bool ffmpeg_pix_fmt_has_alpha(enum AVPixelFormat pix_fmt);
static bool ffmpeg_pix_fmt_is_yuv(enum AVPixelFormat pix_fmt);

#if LV_COLOR_DEPTH != 32
    static void convert_color_depth(uint8_t * img, uint32_t px_cnt);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_ffmpeg_init(void)
{
    lv_img_decoder_t * dec = lv_img_decoder_create(NULL);
    lv_img_decoder_set_accept_cb(dec, decoder_accept);
    lv_img_decoder_set_open_cb(dec, decoder_open);
    lv_img_decoder_set_close_cb(dec, decoder_close);

#if LV_FFMPEG_AV_DUMP_FORMAT == 0
    av_log_set_level(AV_LOG_QUIET);
#endif
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_res_t decoder_accept(const lv_img_src_t * src, uint8_t * caps, void * user_data)
{
    LV_UNUSED(user_data);
    if(src->type == LV_IMG_SRC_FILE) {
        lv_img_header_t header;
        /* Sorry here, there's no other way to accept this source without trying to open it*/
        if(ffmpeg_get_img_header((const char *)src->data, &header) < 0) {
            LV_LOG_ERROR("ffmpeg can't get image header");
            return LV_RES_INV;
        }
        /* We don't fill the TRANSPARENT cap here since it involves too much computation later on */
        *caps = LV_IMG_DEC_ANIMATED | LV_IMG_DEC_CACHED;
        return LV_RES_OK;
    }

    /* If didn't succeeded earlier then it's an error */
    return LV_RES_INV;
}

static lv_res_t decoder_open(lv_img_dec_dsc_t * dsc, const lv_img_dec_flags_t flags, void * user_data)
{
    if(dsc->input.src->type != LV_IMG_SRC_FILE) {
        return LV_RES_INV;
    }

    const char * fn = (const char *)dsc->input.src->data;
    if(!dsc->dec_ctx) {
        LV_ZERO_ALLOC(dsc->dec_ctx);
        if(!dsc->dec_ctx)
            return LV_RES_INV;

        dsc->dec_ctx->auto_allocated = 1;
    }

    struct ffmpeg_context_s * ffmpeg_ctx = (struct ffmpeg_context_s *)dsc->dec_ctx->user_data;
    if(!ffmpeg_ctx) {
        ffmpeg_ctx = ffmpeg_open_file(fn);

        if(ffmpeg_ctx == NULL) goto error_out;

        dsc->dec_ctx->user_data = ffmpeg_ctx;
    }
    /* Extract metadata */
    dsc->dec_ctx->total_frames = ffmpeg_ctx->video_stream->nb_frames;
    if(!dsc->dec_ctx->total_frames) {
        goto error_out;
    }
    dsc->dec_ctx->frame_rate = ffmpeg_ctx->video_stream->avg_frame_rate.num / ffmpeg_ctx->video_stream->avg_frame_rate.den;
    dsc->dec_ctx->frame_delay = (1000 * ffmpeg_ctx->video_stream->avg_frame_rate.den) /
                                ffmpeg_ctx->video_stream->avg_frame_rate.num;
    dsc->caps = LV_IMG_DEC_ANIMATED | LV_IMG_DEC_CACHED;
    dsc->header.w = ffmpeg_ctx->video_dec_ctx->width;
    dsc->header.h = ffmpeg_ctx->video_dec_ctx->height;
    dsc->header.always_zero = 0;
    dsc->header.cf = ffmpeg_ctx->has_alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;

    if(flags == LV_IMG_DEC_ONLYMETA) {
        decoder_close(dsc, user_data);
        return LV_RES_OK;
    }

    if(ffmpeg_ctx->video_src_data[0] == NULL && ffmpeg_image_allocate(ffmpeg_ctx) < 0) {
        LV_LOG_ERROR("ffmpeg image allocate failed");
        goto error_out;
    }

    if(dsc->dec_ctx->current_frame == 0) {
        av_seek_frame(ffmpeg_ctx->fmt_ctx, 0, 0, AVSEEK_FLAG_BACKWARD);
        LV_LOG_INFO("ffmpeg seeking to 0");
    }

    if(dsc->dec_ctx->current_frame != ffmpeg_ctx->last_rendered_frame
       && dsc->dec_ctx->current_frame < dsc->dec_ctx->total_frames) {
        if(ffmpeg_update_next_frame(ffmpeg_ctx) < 0) {
            LV_LOG_ERROR("ffmpeg update frame failed");
            goto error_out;
        }

        dsc->dec_ctx->last_rendering = lv_tick_get();
        ffmpeg_ctx->last_rendered_frame = dsc->dec_ctx->current_frame;
    }
    uint8_t * img_data = ffmpeg_get_img_data(ffmpeg_ctx);

#if LV_COLOR_DEPTH != 32
    if(ffmpeg_ctx->has_alpha) {
        convert_color_depth(img_data, dsc->header.w * dsc->header.h);
    }
#endif

    dsc->img_data = img_data;

    /* The image is fully decoded. Return with its pointer */
    return LV_RES_OK;
error_out:
    decoder_close(dsc, user_data);
    return LV_RES_INV;
}

static void decoder_close(lv_img_dec_dsc_t * dsc, void * user_data)
{
    LV_UNUSED(user_data);
    if(dsc->dec_ctx && dsc->dec_ctx->auto_allocated) {
        struct ffmpeg_context_s * ffmpeg_ctx = (struct ffmpeg_context_s *)dsc->dec_ctx->user_data;
        if(ffmpeg_ctx != NULL) ffmpeg_close(ffmpeg_ctx);
        dsc->dec_ctx->user_data = 0;

        lv_free(dsc->dec_ctx);
        dsc->dec_ctx = 0;
    }
}

#if LV_COLOR_DEPTH != 32

static void convert_color_depth(uint8_t * img, uint32_t px_cnt)
{
    lv_color32_t * img_src_p = (lv_color32_t *)img;
    struct lv_img_pixel_color_s * img_dst_p = (struct lv_img_pixel_color_s *)img;

    for(uint32_t i = 0; i < px_cnt; i++) {
        lv_color32_t temp = *img_src_p;
        img_dst_p->c = lv_color_hex(temp.full);
        img_dst_p->alpha = temp.ch.alpha;

        img_src_p++;
        img_dst_p++;
    }
}

#endif

static uint8_t * ffmpeg_get_img_data(struct ffmpeg_context_s * ffmpeg_ctx)
{
    uint8_t * img_data = ffmpeg_ctx->video_dst_data[0];

    if(img_data == NULL) {
        LV_LOG_ERROR("ffmpeg video dst data is NULL");
    }

    return img_data;
}

static bool ffmpeg_pix_fmt_has_alpha(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor * desc = av_pix_fmt_desc_get(pix_fmt);

    if(desc == NULL) {
        return false;
    }

    if(pix_fmt == AV_PIX_FMT_PAL8) {
        return true;
    }

    return (desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? true : false;
}

static bool ffmpeg_pix_fmt_is_yuv(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor * desc = av_pix_fmt_desc_get(pix_fmt);

    if(desc == NULL) {
        return false;
    }

    return !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 2;
}

static int ffmpeg_output_video_frame(struct ffmpeg_context_s * ffmpeg_ctx)
{
    int ret = -1;

    int width = ffmpeg_ctx->video_dec_ctx->width;
    int height = ffmpeg_ctx->video_dec_ctx->height;
    AVFrame * frame = ffmpeg_ctx->frame;

    if(frame->width != width
       || frame->height != height
       || frame->format != ffmpeg_ctx->video_dec_ctx->pix_fmt) {

        /* To handle this change, one could call av_image_alloc again and
         * decode the following frames into another rawvideo file.
         */
        LV_LOG_ERROR("Width, height and pixel format have to be "
                     "constant in a rawvideo file, but the width, height or "
                     "pixel format of the input video changed:\n"
                     "old: width = %d, height = %d, format = %s\n"
                     "new: width = %d, height = %d, format = %s\n",
                     width,
                     height,
                     av_get_pix_fmt_name(ffmpeg_ctx->video_dec_ctx->pix_fmt),
                     frame->width, frame->height,
                     av_get_pix_fmt_name(frame->format));
        goto failed;
    }

    LV_LOG_TRACE("video_frame coded_n:%d", frame->coded_picture_number);

    /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data
     */
    av_image_copy(ffmpeg_ctx->video_src_data, ffmpeg_ctx->video_src_linesize,
                  (const uint8_t **)(frame->data), frame->linesize,
                  ffmpeg_ctx->video_dec_ctx->pix_fmt, width, height);

    if(ffmpeg_ctx->sws_ctx == NULL) {
        int swsFlags = SWS_BILINEAR;

        if(ffmpeg_pix_fmt_is_yuv(ffmpeg_ctx->video_dec_ctx->pix_fmt)) {

            /* When the video width and height are not multiples of 8,
             * and there is no size change in the conversion,
             * a blurry screen will appear on the right side
             * This problem was discovered in 2012 and
             * continues to exist in version 4.1.3 in 2019
             * This problem can be avoided by increasing SWS_ACCURATE_RND
             */
            if((width & 0x7) || (height & 0x7)) {
                LV_LOG_WARN("The width(%d) and height(%d) the image "
                            "is not a multiple of 8, "
                            "the decoding speed may be reduced",
                            width, height);
                swsFlags |= SWS_ACCURATE_RND;
            }
        }

        ffmpeg_ctx->sws_ctx = sws_getContext(
                                  width, height, ffmpeg_ctx->video_dec_ctx->pix_fmt,
                                  width, height, ffmpeg_ctx->video_dst_pix_fmt,
                                  swsFlags,
                                  NULL, NULL, NULL);
    }

    if(!ffmpeg_ctx->has_alpha) {
        int lv_linesize = sizeof(lv_color_t) * width;
        int dst_linesize = ffmpeg_ctx->video_dst_linesize[0];
        if(dst_linesize != lv_linesize) {
            LV_LOG_WARN("ffmpeg linesize = %d, but lvgl image require %d",
                        dst_linesize,
                        lv_linesize);
            ffmpeg_ctx->video_dst_linesize[0] = lv_linesize;
        }
    }

    ret = sws_scale(
              ffmpeg_ctx->sws_ctx,
              (const uint8_t * const *)(ffmpeg_ctx->video_src_data),
              ffmpeg_ctx->video_src_linesize,
              0,
              height,
              ffmpeg_ctx->video_dst_data,
              ffmpeg_ctx->video_dst_linesize);

failed:
    return ret;
}

static int ffmpeg_decode_packet(AVCodecContext * dec, const AVPacket * pkt,
                                struct ffmpeg_context_s * ffmpeg_ctx)
{
    int ret = 0;

    /* submit the packet to the decoder */
    ret = avcodec_send_packet(dec, pkt);
    if(ret < 0) {
        LV_LOG_ERROR("Error submitting a packet for decoding (%s)",
                     av_err2str(ret));
        return ret;
    }

    /* get all the available frames from the decoder */
    while(ret <= 0) {
        ret = avcodec_receive_frame(dec, ffmpeg_ctx->frame);
        if(ret < 0) {

            /* those two return values are special and mean there is
             * no output frame available,
             * but there were no errors during decoding
             */
            if(ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                return 0; /* No image captured */
            }

            LV_LOG_ERROR("Error during decoding (%s)", av_err2str(ret));
            return ret;
        }

        /* write the frame data to output file */
        if(dec->codec->type == AVMEDIA_TYPE_VIDEO) {
            ret = ffmpeg_output_video_frame(ffmpeg_ctx);
        }

        av_frame_unref(ffmpeg_ctx->frame);
        if(ret < 0) {
            LV_LOG_WARN("ffmpeg_decode_packet ended %d", ret);
            return ret;
        }
    }

    return ret;
}

static int ffmpeg_open_codec_context(int * stream_idx,
                                     AVCodecContext ** dec_ctx, AVFormatContext * fmt_ctx,
                                     enum AVMediaType type)
{
    int ret;
    int stream_index;
    AVStream * st;
    const AVCodec * dec = NULL;
    AVDictionary * opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if(ret < 0) {
        LV_LOG_ERROR("Could not find %s stream in input file",
                     av_get_media_type_string(type));
        return ret;
    }
    else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if(dec == NULL) {
            LV_LOG_ERROR("Failed to find %s codec",
                         av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if(*dec_ctx == NULL) {
            LV_LOG_ERROR("Failed to allocate the %s codec context",
                         av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            LV_LOG_ERROR(
                "Failed to copy %s codec parameters to decoder context",
                av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            LV_LOG_ERROR("Failed to open %s codec",
                         av_get_media_type_string(type));
            return ret;
        }

        *stream_idx = stream_index;
    }

    return 0;
}

static int ffmpeg_get_img_header(const char * filepath,
                                 lv_img_header_t * header)
{
    int ret = -1;

    AVFormatContext * fmt_ctx = NULL;
    AVCodecContext * video_dec_ctx = NULL;
    int video_stream_idx;

    /* open input file, and allocate format context */
    if(avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) {
        LV_LOG_ERROR("Could not open source file %s", filepath);
        goto failed;
    }

    /* retrieve stream information */
    if(avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        LV_LOG_ERROR("Could not find stream information");
        goto failed;
    }

    if(ffmpeg_open_codec_context(&video_stream_idx, &video_dec_ctx,
                                 fmt_ctx, AVMEDIA_TYPE_VIDEO)
       >= 0) {
        bool has_alpha = ffmpeg_pix_fmt_has_alpha(video_dec_ctx->pix_fmt);

        /* allocate image where the decoded image will be put */
        header->w = video_dec_ctx->width;
        header->h = video_dec_ctx->height;
        header->always_zero = 0;
        header->cf = (has_alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR);

        ret = 0;
    }

failed:
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);

    return ret;
}


static int ffmpeg_update_next_frame(struct ffmpeg_context_s * ffmpeg_ctx)
{
    int ret = 0;

    while(1) {

        /* read frames from the file */
        if(av_read_frame(ffmpeg_ctx->fmt_ctx, ffmpeg_ctx->pkt) >= 0) {
            bool is_image = false;

            /* check if the packet belongs to a stream we are interested in,
             * otherwise skip it
             */
            if(ffmpeg_ctx->pkt->stream_index == ffmpeg_ctx->video_stream_idx) {
                ret = ffmpeg_decode_packet(ffmpeg_ctx->video_dec_ctx,
                                           ffmpeg_ctx->pkt, ffmpeg_ctx);
                is_image = ret > 0;
            }

            av_packet_unref(ffmpeg_ctx->pkt);

            if(ret < 0) {
                LV_LOG_WARN("video frame is empty %d", ret);
                break;
            }

            /* Used to filter data that is not an image */
            if(is_image) {
                break;
            }
        }
        else {
            ret = -1;
            break;
        }
    }

    return ret;
}

struct ffmpeg_context_s * ffmpeg_open_file(const char * path)
{
    if(path == NULL || strlen(path) == 0) {
        LV_LOG_ERROR("file path is empty");
        return NULL;
    }

    struct ffmpeg_context_s * ffmpeg_ctx = calloc(1, sizeof(struct ffmpeg_context_s));

    if(ffmpeg_ctx == NULL) {
        LV_LOG_ERROR("ffmpeg_ctx malloc failed");
        goto failed;
    }

    /* open input file, and allocate format context */

    if(avformat_open_input(&(ffmpeg_ctx->fmt_ctx), path, NULL, NULL) < 0) {
        LV_LOG_ERROR("Could not open source file %s", path);
        goto failed;
    }

    /* retrieve stream information */

    if(avformat_find_stream_info(ffmpeg_ctx->fmt_ctx, NULL) < 0) {
        LV_LOG_ERROR("Could not find stream information");
        goto failed;
    }

    if(ffmpeg_open_codec_context(
           &(ffmpeg_ctx->video_stream_idx),
           &(ffmpeg_ctx->video_dec_ctx),
           ffmpeg_ctx->fmt_ctx, AVMEDIA_TYPE_VIDEO)
       >= 0) {
        ffmpeg_ctx->video_stream = ffmpeg_ctx->fmt_ctx->streams[ffmpeg_ctx->video_stream_idx];

        ffmpeg_ctx->has_alpha = ffmpeg_pix_fmt_has_alpha(ffmpeg_ctx->video_dec_ctx->pix_fmt);

        ffmpeg_ctx->video_dst_pix_fmt = (ffmpeg_ctx->has_alpha ? AV_PIX_FMT_BGRA : AV_PIX_FMT_TRUE_COLOR);
    }

#if LV_FFMPEG_AV_DUMP_FORMAT != 0
    /* dump input information to stderr */
    av_dump_format(ffmpeg_ctx->fmt_ctx, 0, path, 0);
#endif

    if(ffmpeg_ctx->video_stream == NULL) {
        LV_LOG_ERROR("Could not find video stream in the input, aborting");
        goto failed;
    }

    ffmpeg_ctx->last_rendered_frame = (lv_frame_index_t) -1;
    return ffmpeg_ctx;

failed:
    ffmpeg_close(ffmpeg_ctx);
    return NULL;
}

static int ffmpeg_image_allocate(struct ffmpeg_context_s * ffmpeg_ctx)
{
    int ret;

    /* allocate image where the decoded image will be put */
    ret = av_image_alloc(
              ffmpeg_ctx->video_src_data,
              ffmpeg_ctx->video_src_linesize,
              ffmpeg_ctx->video_dec_ctx->width,
              ffmpeg_ctx->video_dec_ctx->height,
              ffmpeg_ctx->video_dec_ctx->pix_fmt,
              4);

    if(ret < 0) {
        LV_LOG_ERROR("Could not allocate src raw video buffer");
        return ret;
    }

    LV_LOG_INFO("alloc video_src_bufsize = %d", ret);

    ret = av_image_alloc(
              ffmpeg_ctx->video_dst_data,
              ffmpeg_ctx->video_dst_linesize,
              ffmpeg_ctx->video_dec_ctx->width,
              ffmpeg_ctx->video_dec_ctx->height,
              ffmpeg_ctx->video_dst_pix_fmt,
              4);

    if(ret < 0) {
        LV_LOG_ERROR("Could not allocate dst raw video buffer");
        return ret;
    }

    LV_LOG_INFO("allocate video_dst_bufsize = %d", ret);

    ffmpeg_ctx->frame = av_frame_alloc();

    if(ffmpeg_ctx->frame == NULL) {
        LV_LOG_ERROR("Could not allocate frame");
        return -1;
    }

    /* allocate packet, set data to NULL, let the demuxer fill it */

    ffmpeg_ctx->pkt = av_packet_alloc();
    if(ffmpeg_ctx->pkt == NULL) {
        LV_LOG_ERROR("av_packet_alloc failed");
        return -1;
    }
    ffmpeg_ctx->pkt->data = NULL;
    ffmpeg_ctx->pkt->size = 0;

    return 0;
}

static void ffmpeg_close_src_ctx(struct ffmpeg_context_s * ffmpeg_ctx)
{
    avcodec_free_context(&(ffmpeg_ctx->video_dec_ctx));
    avformat_close_input(&(ffmpeg_ctx->fmt_ctx));
    av_frame_free(&(ffmpeg_ctx->frame));
    if(ffmpeg_ctx->video_src_data[0] != NULL) {
        av_free(ffmpeg_ctx->video_src_data[0]);
        ffmpeg_ctx->video_src_data[0] = NULL;
    }
}

static void ffmpeg_close_dst_ctx(struct ffmpeg_context_s * ffmpeg_ctx)
{
    if(ffmpeg_ctx->video_dst_data[0] != NULL) {
        av_free(ffmpeg_ctx->video_dst_data[0]);
        ffmpeg_ctx->video_dst_data[0] = NULL;
    }
}

static void ffmpeg_close(struct ffmpeg_context_s * ffmpeg_ctx)
{
    if(ffmpeg_ctx == NULL) {
        LV_LOG_WARN("ffmpeg_ctx is NULL");
        return;
    }

    sws_freeContext(ffmpeg_ctx->sws_ctx);
    ffmpeg_close_src_ctx(ffmpeg_ctx);
    ffmpeg_close_dst_ctx(ffmpeg_ctx);
    free(ffmpeg_ctx);

    LV_LOG_INFO("ffmpeg_ctx closed");
}


#endif /*LV_USE_FFMPEG*/
