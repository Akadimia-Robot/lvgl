/**
 * @file lv_img.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_img.h"
#if LV_USE_IMG != 0

/*Testing of dependencies*/
#if LV_USE_LABEL == 0
#error "lv_img: lv_label is required. Enable it in lv_conf.h (LV_USE_LABEL  1) "
#endif

#include "../lv_core/lv_debug.h"
#include "../lv_themes/lv_theme.h"
#include "../lv_draw/lv_img_decoder.h"
#include "../lv_misc/lv_fs.h"
#include "../lv_misc/lv_txt.h"
#include "../lv_misc/lv_math.h"
#include "../lv_misc/lv_log.h"

/*********************
 *      DEFINES
 *********************/
#define LV_OBJX_NAME "lv_img"

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_design_res_t lv_img_design(lv_obj_t * img, const lv_area_t * clip_area, lv_design_mode_t mode);
static lv_res_t lv_img_signal(lv_obj_t * img, lv_signal_t sign, void * param);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_signal_cb_t ancestor_signal;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create an image objects
 * @param par pointer to an object, it will be the parent of the new button
 * @param copy pointer to a image object, if not NULL then the new object will be copied from it
 * @return pointer to the created image
 */
lv_obj_t * lv_img_create(lv_obj_t * par, const lv_obj_t * copy)
{
    LV_LOG_TRACE("image create started");

    lv_obj_t * new_img = NULL;

    /*Create a basic object*/
    new_img = lv_obj_create(par, copy);
    LV_ASSERT_MEM(new_img);
    if(new_img == NULL) return NULL;

    if(ancestor_signal == NULL) ancestor_signal = lv_obj_get_signal_cb(new_img);

    /*Extend the basic object to image object*/
    lv_img_ext_t * ext = lv_obj_allocate_ext_attr(new_img, sizeof(lv_img_ext_t));
    LV_ASSERT_MEM(ext);
    if(ext == NULL) return NULL;

    ext->src       = NULL;
    ext->src_type  = LV_IMG_SRC_UNKNOWN;
    ext->cf        = LV_IMG_CF_UNKNOWN;
    ext->w         = lv_obj_get_width(new_img);
    ext->h         = lv_obj_get_height(new_img);
    ext->angle = 0;
    ext->zoom = LV_IMG_ZOOM_NONE;
    ext->antialias = LV_ANTIALIAS ? 1 : 0;
    ext->auto_size = 1;
    ext->offset.x  = 0;
    ext->offset.y  = 0;
    ext->pivot.x = 0;
    ext->pivot.y = 0;

    /*Init the new object*/
    lv_obj_set_signal_cb(new_img, lv_img_signal);
    lv_obj_set_design_cb(new_img, lv_img_design);

    if(copy == NULL) {
        lv_obj_set_click(new_img, false);
        /* Enable auto size for non screens
         * because image screens are wallpapers
         * and must be screen sized*/
        if(par != NULL) {
            ext->auto_size = 1;
            lv_obj_set_style(new_img, NULL); /*Inherit the style  by default*/
        } else {
            ext->auto_size = 0;
            lv_obj_set_style(new_img, &lv_style_plain); /*Set a style for screens*/
        }
    } else {
        lv_img_ext_t * copy_ext = lv_obj_get_ext_attr(copy);
        ext->auto_size          = copy_ext->auto_size;
        lv_img_set_src(new_img, copy_ext->src);

        /*Refresh the style with new signal function*/
        lv_obj_refresh_style(new_img);
    }

    LV_LOG_INFO("image created");

    return new_img;
}

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the pixel map to display by the image
 * @param img pointer to an image object
 * @param data the image data
 */
void lv_img_set_src(lv_obj_t * img, const void * src_img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_src_t src_type = lv_img_src_get_type(src_img);
    lv_img_ext_t * ext    = lv_obj_get_ext_attr(img);

#if LV_USE_LOG && LV_LOG_LEVEL >= LV_LOG_LEVEL_INFO
    switch(src_type) {
        case LV_IMG_SRC_FILE: LV_LOG_TRACE("lv_img_set_src: `LV_IMG_SRC_FILE` type found"); break;
        case LV_IMG_SRC_VARIABLE: LV_LOG_TRACE("lv_img_set_src: `LV_IMG_SRC_VARIABLE` type found"); break;
        case LV_IMG_SRC_SYMBOL: LV_LOG_TRACE("lv_img_set_src: `LV_IMG_SRC_SYMBOL` type found"); break;
        default: LV_LOG_WARN("lv_img_set_src: unknown type");
    }
#endif

    /*If the new source type is unknown free the memories of the old source*/
    if(src_type == LV_IMG_SRC_UNKNOWN) {
        LV_LOG_WARN("lv_img_set_src: unknown image type");
        if(ext->src_type == LV_IMG_SRC_SYMBOL || ext->src_type == LV_IMG_SRC_FILE) {
            lv_mem_free(ext->src);
        }
        ext->src      = NULL;
        ext->src_type = LV_IMG_SRC_UNKNOWN;
        return;
    }

    lv_img_header_t header;
    lv_img_decoder_get_info(src_img, &header);

    /*Save the source*/
    if(src_type == LV_IMG_SRC_VARIABLE) {
        LV_LOG_INFO("lv_img_set_src:  `LV_IMG_SRC_VARIABLE` type found");

        /*If memory was allocated because of the previous `src_type` then free it*/
        if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_SYMBOL) {
            lv_mem_free(ext->src);
        }
        ext->src = src_img;
    } else if(src_type == LV_IMG_SRC_FILE || src_type == LV_IMG_SRC_SYMBOL) {
        /* If the new and the old src are the same then it was only a refresh.*/
        if(ext->src != src_img) {
            const void * old_src = NULL;
            /* If memory was allocated because of the previous `src_type` then save its pointer and free after allocation.
             * It's important to allocate first to be sure the new data will be on a new address.
             * Else `img_cache` wouldn't see the change in source.*/
            if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_SYMBOL) {
                old_src = ext->src;
            }
            char * new_str = lv_mem_alloc(strlen(src_img) + 1);
            LV_ASSERT_MEM(new_str);
            if(new_str == NULL) return;
            strcpy(new_str, src_img);
            ext->src = new_str;

            if(old_src) lv_mem_free(old_src);
        }
    }

    if(src_type == LV_IMG_SRC_SYMBOL) {
        /*`lv_img_dsc_get_info` couldn't set the with and height of a font so set it here*/
        const lv_style_t * style = lv_img_get_style(img, LV_IMG_STYLE_MAIN);
        lv_point_t size;
        lv_txt_get_size(&size, src_img, style->text.font, style->text.letter_space, style->text.line_space,
                        LV_COORD_MAX, LV_TXT_FLAG_NONE);
        header.w = size.x;
        header.h = size.y;
    }

    ext->src_type = src_type;
    ext->w        = header.w;
    ext->h        = header.h;
    ext->cf       = header.cf;
    ext->pivot.x = header.w / 2;
    ext->pivot.y = header.h / 2;

    if(lv_img_get_auto_size(img) != false) {
        lv_obj_set_size(img, ext->w, ext->h);
    }

    /*Provide enough room for the rotated corners*/
    if(ext->angle) lv_obj_refresh_ext_draw_pad(img);

    lv_obj_invalidate(img);
}

/**
 * Enable the auto size feature.
 * If enabled the object size will be same as the picture size.
 * @param img pointer to an image
 * @param en true: auto size enable, false: auto size disable
 */
void lv_img_set_auto_size(lv_obj_t * img, bool en)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    ext->auto_size = (en == false ? 0 : 1);
}

/**
 * Set an offset for the source of an image.
 * so the image will be displayed from the new origin.
 * @param img pointer to an image
 * @param x: the new offset along x axis.
 */
void lv_img_set_offset_x(lv_obj_t * img, lv_coord_t x)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    x = x % ext->w;

    ext->offset.x = x;
    lv_obj_invalidate(img);
}

/**
 * Set an offset for the source of an image.
 * so the image will be displayed from the new origin.
 * @param img pointer to an image
 * @param y: the new offset along y axis.
 */
void lv_img_set_offset_y(lv_obj_t * img, lv_coord_t y)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    y = y % ext->h;

    ext->offset.y = y;
    lv_obj_invalidate(img);
}

/**
 * Set the rotation center of the image.
 * The image will be rotated around this point
 * @param img pointer to an image object
 * @param pivot_x rotation center x of the image
 * @param pivot_y rotation center y of the image
 */
void lv_img_set_pivot(lv_obj_t * img, lv_coord_t pivot_x, lv_coord_t pivot_y)
{
    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
	if (ext->pivot.x == pivot_x && ext->pivot.y == pivot_y) return;

    lv_obj_invalidate(img);
    ext->pivot.x = pivot_x;
    ext->pivot.y = pivot_y;
    lv_obj_refresh_ext_draw_pad(img);
    lv_obj_invalidate(img);
}

/**
 * Set the rotation angle of the image.
 * The image will be rotated around its middle point
 * @param img pointer to an image object
 * @param angle rotation angle in degree (> 0: clock wise)
 */
void lv_img_set_angle(lv_obj_t * img, int16_t angle)
{
    if(angle < 0 || angle >= 360) angle = angle % 360;

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(angle == ext->angle) return;

    lv_obj_invalidate(img);
    ext->angle = angle;
    lv_obj_refresh_ext_draw_pad(img);
    lv_obj_invalidate(img);
}

/**
 * Set the zoom factor of the image.
 * @param img pointer to an image object
 * @param zoom the zoom factor.
 * - 256 or LV_ZOOM_IMG_NONE for no zoom
 * - <256: scale down
 * - >256 scale up
 * - 128 half size
 * - 512 double size
 */
void lv_img_set_zoom(lv_obj_t * img, uint16_t zoom)
{
    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(zoom == ext->zoom) return;

    if(zoom == 0) zoom = 1;

    lv_obj_invalidate(img);
    ext->zoom = zoom;
    lv_obj_refresh_ext_draw_pad(img);
    lv_obj_invalidate(img);
}

/**
 * Enable/disable anti-aliasing for the transformations (rotate, zoom) or not
 * @param img pointer to an image object
 * @param antialias true: anti-aliased; false: not anti-aliased
 */
void lv_img_set_antialias(lv_obj_t * img, bool antialias)
{
    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(antialias == ext->antialias) return;

    ext->antialias = antialias;
    lv_obj_invalidate(img);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Get the source of the image
 * @param img pointer to an image object
 * @return the image source (symbol, file name or C array)
 */
const void * lv_img_get_src(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->src;
}

/**
 * Get the name of the file set for an image
 * @param img pointer to an image
 * @return file name
 */
const char * lv_img_get_file_name(const lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    if(ext->src_type == LV_IMG_SRC_FILE)
        return ext->src;
    else
        return "";
}

/**
 * Get the auto size enable attribute
 * @param img pointer to an image
 * @return true: auto size is enabled, false: auto size is disabled
 */
bool lv_img_get_auto_size(const lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->auto_size == 0 ? false : true;
}

/**
 * Get the offset.x attribute of the img object.
 * @param img pointer to an image
 * @return offset.x value.
 */
lv_coord_t lv_img_get_offset_x(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->offset.x;
}

/**
 * Get the offset.y attribute of the img object.
 * @param img pointer to an image
 * @return offset.y value.
 */
lv_coord_t lv_img_get_offset_y(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->offset.y;
}

/**
 * Get the rotation center of the image.
 * @param img pointer to an image object
 * @param center rotation center of the image
 */
void lv_img_get_pivot(lv_obj_t * img, lv_point_t *pivot)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    *pivot = ext->pivot;
}

/**
 * Get the rotation angle of the image.
 * @param img pointer to an image object
 * @return rotation angle in degree (0..359)
 */
uint16_t lv_img_get_angle(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->angle;
}

/**
 * Get the zoom factor of the image.
 * @param img pointer to an image object
 * @return zoom factor (256: no zoom)
 */
uint16_t lv_img_get_zoom(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->zoom;
}

/**
 * Get whether the transformations (rotate, zoom) are anti-aliased or not
 * @param img pointer to an image object
 * @return true: anti-aliased; false: not anti-aliased
 */
bool lv_img_get_antialias(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->antialias ? true : false;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Handle the drawing related tasks of the images
 * @param img pointer to an object
 * @param clip_area the object will be drawn only in this area
 * @param mode LV_DESIGN_COVER_CHK: only check if the object fully covers the 'mask_p' area
 *                                  (return 'true' if yes)
 *             LV_DESIGN_DRAW: draw the object (always return 'true')
 *             LV_DESIGN_DRAW_POST: drawing after every children are drawn
 * @param return an element of `lv_design_res_t`
 */
static lv_design_res_t lv_img_design(lv_obj_t * img, const lv_area_t * clip_area, lv_design_mode_t mode)
{
    const lv_style_t * style = lv_obj_get_style(img);
    lv_img_ext_t * ext       = lv_obj_get_ext_attr(img);

    if(mode == LV_DESIGN_COVER_CHK) {
        lv_design_res_t cover = LV_DESIGN_RES_NOT_COVER;
        if(ext->src_type == LV_IMG_SRC_UNKNOWN || ext->src_type == LV_IMG_SRC_SYMBOL || ext->angle != 0) return LV_DESIGN_RES_NOT_COVER;

        if(ext->cf == LV_IMG_CF_TRUE_COLOR || ext->cf == LV_IMG_CF_RAW) {
            cover = lv_area_is_in(clip_area, &img->coords) ? LV_DESIGN_RES_COVER : LV_DESIGN_RES_NOT_COVER;
        }

        return cover;
    } else if(mode == LV_DESIGN_DRAW_MAIN) {
        if(ext->h == 0 || ext->w == 0) return true;
        lv_area_t coords;
        lv_opa_t opa_scale = lv_obj_get_opa_scale(img);

        lv_obj_get_coords(img, &coords);

        if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_VARIABLE) {
            coords.x1 += ext->offset.x;
            coords.y1 += ext->offset.y;

            if(coords.x1 > img->coords.x1) coords.x1 -= ext->w;
            if(coords.y1 > img->coords.y1) coords.y1 -= ext->h;

            LV_LOG_TRACE("lv_img_design: start to draw image");
            lv_area_t cords_tmp;
            cords_tmp.y1 = coords.y1;
            cords_tmp.y2 = coords.y1 + ext->h - 1;

            for(; cords_tmp.y1 <= coords.y2; cords_tmp.y1 += ext->h, cords_tmp.y2 += ext->h) {
                cords_tmp.x1 = coords.x1;
                cords_tmp.x2 = coords.x1 + ext->w - 1;
                for(; cords_tmp.x1 <= coords.x2; cords_tmp.x1 += ext->w, cords_tmp.x2 += ext->w) {
                    lv_draw_img(&cords_tmp, clip_area, ext->src, style, ext->angle, &ext->pivot, ext->zoom, ext->antialias, opa_scale);
                }
            }
        } else if(ext->src_type == LV_IMG_SRC_SYMBOL) {
            LV_LOG_TRACE("lv_img_design: start to draw symbol");
            lv_style_t style_mod;
            lv_style_copy(&style_mod, style);
            style_mod.text.color = style->image.color;
            lv_draw_label(&coords, clip_area, &style_mod, opa_scale, ext->src, LV_TXT_FLAG_NONE, NULL, NULL, NULL, lv_obj_get_base_dir(img));
        } else {
            /*Trigger the error handler of image drawer*/
            LV_LOG_WARN("lv_img_design: image source type is unknown");
            lv_draw_img(&img->coords, clip_area, NULL, style, 0, NULL, LV_IMG_ZOOM_NONE, false, opa_scale);
        }
    }

    return true;
}

/**
 * Signal function of the image
 * @param img pointer to an image object
 * @param sign a signal type from lv_signal_t enum
 * @param param pointer to a signal specific variable
 * @return LV_RES_OK: the object is not deleted in the function; LV_RES_INV: the object is deleted
 */
static lv_res_t lv_img_signal(lv_obj_t * img, lv_signal_t sign, void * param)
{
    lv_res_t res;

    /* Include the ancient signal function */
    res = ancestor_signal(img, sign, param);
    if(res != LV_RES_OK) return res;

    if(sign == LV_SIGNAL_GET_TYPE) return lv_obj_handle_get_type_signal(param, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(sign == LV_SIGNAL_CLEANUP) {
        if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_SYMBOL) {
            lv_mem_free(ext->src);
            ext->src      = NULL;
            ext->src_type = LV_IMG_SRC_UNKNOWN;
        }
    } else if(sign == LV_SIGNAL_STYLE_CHG) {
        /*Refresh the file name to refresh the symbol text size*/
        if(ext->src_type == LV_IMG_SRC_SYMBOL) {
            lv_img_set_src(img, ext->src);
        }
    } else if(sign == LV_SIGNAL_REFR_EXT_DRAW_PAD) {
        /*If the image has angle provide enough room for the rotated corners */
        if(ext->angle || ext->zoom != LV_IMG_ZOOM_NONE) {
            lv_sqrt_res_t ds;
            lv_coord_t max_w = ext->w + LV_MATH_ABS(ext->pivot.x + ext->w / 2);
            lv_coord_t max_h = ext->h + LV_MATH_ABS(ext->pivot.y + ext->h / 2);
			lv_sqrt(max_w * max_w + max_h * max_h, &ds);/*Maximum diagonal length*/
			lv_sqrt(ds.i * ds.i + ds.i * ds.i, &ds);    /*Maximum side length of external rectangle*/
            ds.i = (ds.i * ext->zoom ) >> 8;         /*+10 to be sure anything won't be clipped*/

            lv_coord_t d = ds.i / 2;
            img->ext_draw_pad = LV_MATH_MAX(img->ext_draw_pad, d);
        }
    }

    return res;
}

#endif
