/**
 * @file lv_draw_pxp_blend.c
 *
 */

/**
 * Copyright 2020-2023 NXP
 *
 * SPDX-License-Identifier: MIT
 */

#if 0
/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_pxp_blend.h"

#if LV_USE_DRAW_PXP
#include "lvgl_support.h"

/*********************
 *      DEFINES
 *********************/

#if LV_COLOR_DEPTH == 16
    #define PXP_OUT_PIXEL_FORMAT kPXP_OutputPixelFormatRGB565
    #define PXP_AS_PIXEL_FORMAT kPXP_AsPixelFormatRGB565
    #define PXP_PS_PIXEL_FORMAT kPXP_PsPixelFormatRGB565
    #define PXP_TEMP_BUF_SIZE LCD_WIDTH * LCD_HEIGHT * 2U
#elif LV_COLOR_DEPTH == 32
    #define PXP_OUT_PIXEL_FORMAT kPXP_OutputPixelFormatARGB8888
    #define PXP_AS_PIXEL_FORMAT kPXP_AsPixelFormatARGB8888
    #if (!(defined(FSL_FEATURE_PXP_HAS_NO_EXTEND_PIXEL_FORMAT) && FSL_FEATURE_PXP_HAS_NO_EXTEND_PIXEL_FORMAT)) && \
        (!(defined(FSL_FEATURE_PXP_V3) && FSL_FEATURE_PXP_V3))
        #define PXP_PS_PIXEL_FORMAT kPXP_PsPixelFormatARGB8888
    #else
        #define PXP_PS_PIXEL_FORMAT kPXP_PsPixelFormatRGB888
    #endif
    #define PXP_TEMP_BUF_SIZE LCD_WIDTH * LCD_HEIGHT * 4U
#elif
    #error Only 16bit and 32bit color depth are supported. Set LV_COLOR_DEPTH to 16 or 32.
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static LV_ATTRIBUTE_MEM_ALIGN uint8_t temp_buf[PXP_TEMP_BUF_SIZE];

/**
 * BLock Image Transfer - copy rectangular image from src buffer to dst buffer
 * with combination of transformation (rotation, scale, recolor) and opacity, alpha channel
 * or color keying. This requires two steps. First step is used for transformation into
 * a temporary buffer and the second one will handle the color format or opacity.
 *
 * @param[in/out] dest_buf Destination buffer
 * @param[in] dest_area Area with relative coordinates of destination buffer
 * @param[in] dest_stride Stride of destination buffer in pixels
 * @param[in] src_buf Source buffer
 * @param[in] src_area Area with relative coordinates of source buffer
 * @param[in] src_stride Stride of source buffer in pixels
 * @param[in] dsc Image descriptor
 * @param[in] cf Color format
 */
static void lv_pxp_blit_opa(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,
                            const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                            const lv_draw_img_dsc_t * dsc, lv_color_format_t cf);

/**
 * BLock Image Transfer - copy rectangular image from src buffer to dst buffer
 * with transformation and full opacity.
 *
 * @param[in/out] dest_buf Destination buffer
 * @param[in] dest_area Area with relative coordinates of destination buffer
 * @param[in] dest_stride Stride of destination buffer in pixels
 * @param[in] src_buf Source buffer
 * @param[in] src_area Area with relative coordinates of source buffer
 * @param[in] src_stride Stride of source buffer in pixels
 * @param[in] dsc Image descriptor
 * @param[in] cf Color format
 */
static void lv_pxp_blit_cover(lv_color_t * dest_buf, lv_area_t * dest_area, lv_coord_t dest_stride,
                              const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                              const lv_draw_img_dsc_t * dsc, lv_color_format_t cf);

/**
 * BLock Image Transfer - copy rectangular image from src buffer to dst buffer
 * without transformation but handling color format or opacity.
 *
 * @param[in/out] dest_buf Destination buffer
 * @param[in] dest_area Area with relative coordinates of destination buffer
 * @param[in] dest_stride Stride of destination buffer in pixels
 * @param[in] src_buf Source buffer
 * @param[in] src_area Area with relative coordinates of source buffer
 * @param[in] src_stride Stride of source buffer in pixels
 * @param[in] dsc Image descriptor
 * @param[in] cf Color format
 */
static void lv_pxp_blit_cf(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,
                           const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                           const lv_draw_img_dsc_t * dsc, lv_color_format_t cf);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_gpu_nxp_pxp_blit_transform(lv_color_t * dest_buf, lv_area_t * dest_area, lv_coord_t dest_stride,
                                   const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                                   const lv_draw_img_dsc_t * dsc, lv_color_format_t cf)
{
    bool has_recolor = (dsc->recolor_opa != LV_OPA_TRANSP);
    bool has_rotation = (dsc->angle != 0);

    if(has_recolor || has_rotation) {
        if(dsc->opa >= (lv_opa_t)LV_OPA_MAX && !lv_color_format_has_alpha(cf) && cf != LV_COLOR_FORMAT_NATIVE_CHROMA_KEYED) {
            lv_pxp_blit_cover(dest_buf, dest_area, dest_stride, src_buf, src_area, src_stride, dsc, cf);
            return;
        }
        else {
            /*Recolor and/or rotation with alpha or opacity is done in two steps.*/
            lv_pxp_blit_opa(dest_buf, dest_area, dest_stride, src_buf, src_area, src_stride, dsc, cf);
            return;
        }
    }

    lv_pxp_blit_cf(dest_buf, dest_area, dest_stride, src_buf, src_area, src_stride, dsc, cf);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_pxp_blit_opa(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,
                            const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                            const lv_draw_img_dsc_t * dsc, lv_color_format_t cf)
{
    lv_area_t temp_area;
    lv_area_copy(&temp_area, dest_area);
    lv_coord_t temp_stride = dest_stride;
    lv_coord_t temp_w = lv_area_get_width(&temp_area);
    lv_coord_t temp_h = lv_area_get_height(&temp_area);

    /*Step 1: Transform with full opacity to temporary buffer*/
    lv_pxp_blit_cover((lv_color_t *)temp_buf, &temp_area, temp_stride, src_buf, src_area, src_stride, dsc, cf);

    /*Switch width and height if angle requires it*/
    if(dsc->angle == 900 || dsc->angle == 2700) {
        temp_area.x2 = temp_area.x1 + temp_h - 1;
        temp_area.y2 = temp_area.y1 + temp_w - 1;
    }

    /*Step 2: Blit temporary result with required opacity to output*/
    lv_pxp_blit_cf(dest_buf, &temp_area, dest_stride, (lv_color_t *)temp_buf, &temp_area, temp_stride, dsc, cf);
}
static void lv_pxp_blit_cover(lv_color_t * dest_buf, lv_area_t * dest_area, lv_coord_t dest_stride,
                              const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                              const lv_draw_img_dsc_t * dsc, lv_color_format_t cf)
{
    lv_coord_t dest_w = lv_area_get_width(dest_area);
    lv_coord_t dest_h = lv_area_get_height(dest_area);
    lv_coord_t src_w = lv_area_get_width(src_area);
    lv_coord_t src_h = lv_area_get_height(src_area);

    bool has_recolor = (dsc->recolor_opa != LV_OPA_TRANSP);
    bool has_rotation = (dsc->angle != 0);

    lv_point_t pivot = dsc->pivot;
    lv_coord_t piv_offset_x;
    lv_coord_t piv_offset_y;

    lv_pxp_reset();

    if(has_rotation) {
        /*Convert rotation angle and calculate offsets caused by pivot*/
        pxp_rotate_degree_t pxp_angle;
        switch(dsc->angle) {
            case 0:
                pxp_angle = kPXP_Rotate0;
                piv_offset_x = 0;
                piv_offset_y = 0;
                break;
            case 900:
                piv_offset_x = pivot.x + pivot.y - dest_h;
                piv_offset_y = pivot.y - pivot.x;
                pxp_angle = kPXP_Rotate90;
                break;
            case 1800:
                piv_offset_x = 2 * pivot.x - dest_w;
                piv_offset_y = 2 * pivot.y - dest_h;
                pxp_angle = kPXP_Rotate180;
                break;
            case 2700:
                piv_offset_x = pivot.x - pivot.y;
                piv_offset_y = pivot.x + pivot.y - dest_w;
                pxp_angle = kPXP_Rotate270;
                break;
            default:
                piv_offset_x = 0;
                piv_offset_y = 0;
                pxp_angle = kPXP_Rotate0;
        }
        PXP_SetRotateConfig(PXP_ID, kPXP_RotateOutputBuffer, pxp_angle, kPXP_FlipDisable);
        lv_area_move(dest_area, piv_offset_x, piv_offset_y);
    }

    /*AS buffer - source image*/
    pxp_as_buffer_config_t asBufferConfig = {
        .pixelFormat = PXP_AS_PIXEL_FORMAT,
        .bufferAddr = (uint32_t)(src_buf + src_stride * src_area->y1 + src_area->x1),
        .pitchBytes = src_stride * sizeof(lv_color_t)
    };
    PXP_SetAlphaSurfaceBufferConfig(PXP_ID, &asBufferConfig);
    PXP_SetAlphaSurfacePosition(PXP_ID, 0U, 0U, src_w - 1U, src_h - 1U);

    /*Disable PS buffer*/
    PXP_SetProcessSurfacePosition(PXP_ID, 0xFFFFU, 0xFFFFU, 0U, 0U);
    if(has_recolor)
        /*Use as color generator*/
        PXP_SetProcessSurfaceBackGroundColor(PXP_ID, lv_color_to32(dsc->recolor));

    /*Output buffer*/
    pxp_output_buffer_config_t outputBufferConfig = {
        .pixelFormat = (pxp_output_pixel_format_t)PXP_OUT_PIXEL_FORMAT,
        .interlacedMode = kPXP_OutputProgressive,
        .buffer0Addr = (uint32_t)(dest_buf + dest_stride * dest_area->y1 + dest_area->x1),
        .buffer1Addr = (uint32_t)0U,
        .pitchBytes = dest_stride * sizeof(lv_color_t),
        .width = dest_w,
        .height = dest_h
    };
    PXP_SetOutputBufferConfig(PXP_ID, &outputBufferConfig);

    if(has_recolor || lv_color_format_has_alpha(cf)) {
        /**
         * Configure Porter-Duff blending.
         *
         * Note: srcFactorMode and dstFactorMode are inverted in fsl_pxp.h:
         * srcFactorMode is actually applied on PS alpha value
         * dstFactorMode is actually applied on AS alpha value
         */
        pxp_porter_duff_config_t pdConfig = {
            .enable = 1,
            .dstColorMode = kPXP_PorterDuffColorWithAlpha,
            .srcColorMode = kPXP_PorterDuffColorNoAlpha,
            .dstGlobalAlphaMode = kPXP_PorterDuffGlobalAlpha,
            .srcGlobalAlphaMode = lv_color_format_has_alpha(cf) ? kPXP_PorterDuffLocalAlpha : kPXP_PorterDuffGlobalAlpha,
            .dstFactorMode = kPXP_PorterDuffFactorStraight,
            .srcFactorMode = kPXP_PorterDuffFactorInversed,
            .dstGlobalAlpha = has_recolor ? dsc->recolor_opa : 0x00,
            .srcGlobalAlpha = 0xff,
            .dstAlphaMode = kPXP_PorterDuffAlphaStraight, /*don't care*/
            .srcAlphaMode = kPXP_PorterDuffAlphaStraight
        };
        PXP_SetPorterDuffConfig(PXP_ID, &pdConfig);
    }

    lv_pxp_run();
}

static void lv_pxp_blit_cf(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,
                           const lv_color_t * src_buf, const lv_area_t * src_area, lv_coord_t src_stride,
                           const lv_draw_img_dsc_t * dsc, lv_color_format_t cf)
{
    lv_coord_t dest_w = lv_area_get_width(dest_area);
    lv_coord_t dest_h = lv_area_get_height(dest_area);
    lv_coord_t src_w = lv_area_get_width(src_area);
    lv_coord_t src_h = lv_area_get_height(src_area);

    lv_pxp_reset();

    pxp_as_blend_config_t asBlendConfig = {
        .alpha = dsc->opa,
        .invertAlpha = false,
        .alphaMode = kPXP_AlphaRop,
        .ropMode = kPXP_RopMergeAs
    };

    if(dsc->opa >= (lv_opa_t)LV_OPA_MAX && !lv_color_format_has_alpha(cf) && cf != LV_COLOR_FORMAT_NATIVE_CHROMA_KEYED) {
        /*Simple blit, no effect - Disable PS buffer*/
        PXP_SetProcessSurfacePosition(PXP_ID, 0xFFFFU, 0xFFFFU, 0U, 0U);
    }
    else {
        /*PS must be enabled to fetch background pixels.
          PS and OUT buffers are the same, blend will be done in-place*/
        pxp_ps_buffer_config_t psBufferConfig = {
            .pixelFormat = PXP_PS_PIXEL_FORMAT,
            .swapByte = false,
            .bufferAddr = (uint32_t)(dest_buf + dest_stride * dest_area->y1 + dest_area->x1),
            .bufferAddrU = 0U,
            .bufferAddrV = 0U,
            .pitchBytes = dest_stride * sizeof(lv_color_t)
        };
        if(dsc->opa >= (lv_opa_t)LV_OPA_MAX) {
            asBlendConfig.alphaMode = lv_color_format_has_alpha(cf) ? kPXP_AlphaEmbedded : kPXP_AlphaOverride;
        }
        else {
            asBlendConfig.alphaMode = lv_color_format_has_alpha(cf) ? kPXP_AlphaMultiply : kPXP_AlphaOverride;
        }
        PXP_SetProcessSurfaceBufferConfig(PXP_ID, &psBufferConfig);
        PXP_SetProcessSurfacePosition(PXP_ID, 0U, 0U, dest_w - 1U, dest_h - 1U);
    }

    /*AS buffer - source image*/
    pxp_as_buffer_config_t asBufferConfig = {
        .pixelFormat = PXP_AS_PIXEL_FORMAT,
        .bufferAddr = (uint32_t)(src_buf + src_stride * src_area->y1 + src_area->x1),
        .pitchBytes = src_stride * sizeof(lv_color_t)
    };
    PXP_SetAlphaSurfaceBufferConfig(PXP_ID, &asBufferConfig);
    PXP_SetAlphaSurfacePosition(PXP_ID, 0U, 0U, src_w - 1U, src_h - 1U);
    PXP_SetAlphaSurfaceBlendConfig(PXP_ID, &asBlendConfig);

    if(cf == LV_COLOR_FORMAT_NATIVE_CHROMA_KEYED) {
        lv_color_t colorKeyLow = LV_COLOR_CHROMA_KEY;
        lv_color_t colorKeyHigh = LV_COLOR_CHROMA_KEY;

        bool has_recolor = (dsc->recolor_opa != LV_OPA_TRANSP);

        if(has_recolor) {
            /* New color key after recoloring */
            lv_color_t colorKey =  lv_color_mix(dsc->recolor, LV_COLOR_CHROMA_KEY, dsc->recolor_opa);

            LV_COLOR_SET_R(colorKeyLow, colorKey.ch.red != 0 ? colorKey.ch.red - 1 : 0);
            LV_COLOR_SET_G(colorKeyLow, colorKey.ch.green != 0 ? colorKey.ch.green - 1 : 0);
            LV_COLOR_SET_B(colorKeyLow, colorKey.ch.blue != 0 ? colorKey.ch.blue - 1 : 0);

#if LV_COLOR_DEPTH == 16
            LV_COLOR_SET_R(colorKeyHigh, colorKey.ch.red != 0x1f ? colorKey.ch.red + 1 : 0x1f);
            LV_COLOR_SET_G(colorKeyHigh, colorKey.ch.green != 0x3f ? colorKey.ch.green + 1 : 0x3f);
            LV_COLOR_SET_B(colorKeyHigh, colorKey.ch.blue != 0x1f ? colorKey.ch.blue + 1 : 0x1f);
#else /*LV_COLOR_DEPTH == 32*/
            LV_COLOR_SET_R(colorKeyHigh, colorKey.ch.red != 0xff ? colorKey.ch.red + 1 : 0xff);
            LV_COLOR_SET_G(colorKeyHigh, colorKey.ch.green != 0xff ? colorKey.ch.green + 1 : 0xff);
            LV_COLOR_SET_B(colorKeyHigh, colorKey.ch.blue != 0xff ? colorKey.ch.blue + 1 : 0xff);
#endif
        }

        PXP_SetAlphaSurfaceOverlayColorKey(PXP_ID, lv_color_to32(colorKeyLow),
                                           lv_color_to32(colorKeyHigh));
    }

    PXP_EnableAlphaSurfaceOverlayColorKey(PXP_ID, (cf == LV_COLOR_FORMAT_NATIVE_CHROMA_KEYED));

    /*Output buffer.*/
    pxp_output_buffer_config_t outputBufferConfig = {
        .pixelFormat = (pxp_output_pixel_format_t)PXP_OUT_PIXEL_FORMAT,
        .interlacedMode = kPXP_OutputProgressive,
        .buffer0Addr = (uint32_t)(dest_buf + dest_stride * dest_area->y1 + dest_area->x1),
        .buffer1Addr = (uint32_t)0U,
        .pitchBytes = dest_stride * sizeof(lv_color_t),
        .width = dest_w,
        .height = dest_h
    };
    PXP_SetOutputBufferConfig(PXP_ID, &outputBufferConfig);

    lv_pxp_run();
}

#endif /*LV_USE_DRAW_PXP*/
#endif
