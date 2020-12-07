/**
 * @file lv_cb.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_checkbox.h"
#if LV_USE_CHECKBOX != 0

#include "../lv_misc/lv_debug.h"
#include "../lv_core/lv_group.h"
#include "../lv_themes/lv_theme.h"
#include "lv_label.h"

/*********************
 *      DEFINES
 *********************/
#define LV_OBJX_NAME "lv_checkbox"

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_design_res_t lv_checkbox_design(lv_obj_t * cb, const lv_area_t * clip_area, lv_design_mode_t mode);
static lv_res_t lv_checkbox_signal(lv_obj_t * cb, lv_signal_t sign, void * param);
static lv_style_list_t * lv_checkbox_get_style(lv_obj_t * cb, uint8_t type);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_signal_cb_t ancestor_signal;
static lv_design_cb_t ancestor_design;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create a check box objects
 * @param par pointer to an object, it will be the parent of the new check box
 * @param copy pointer to a check box object, if not NULL then the new object will be copied from it
 * @return pointer to the created check box
 */
lv_obj_t * lv_checkbox_create(lv_obj_t * par, const lv_obj_t * copy)
{
    LV_LOG_TRACE("check box create started");

    /*Create the ancestor basic object*/
    lv_obj_t * cb = lv_obj_create(par, copy);
    LV_ASSERT_MEM(cb);
    if(cb == NULL) return NULL;

    if(ancestor_signal == NULL) ancestor_signal = lv_obj_get_signal_cb(cb);
    if(ancestor_design == NULL) ancestor_design = lv_obj_get_design_cb(cb);

    lv_checkbox_ext_t * ext = lv_obj_allocate_ext_attr(cb, sizeof(lv_checkbox_ext_t));
    LV_ASSERT_MEM(ext);
    if(ext == NULL) {
        lv_obj_del(cb);
        return NULL;
    }

    lv_style_list_init(&ext->style_bullet);

    lv_obj_set_signal_cb(cb, lv_checkbox_signal);
    lv_obj_set_design_cb(cb, lv_checkbox_design);

    /*Init the new checkbox object*/
    if(copy == NULL) {
        ext->txt = "Check box";
        ext->static_txt = 1;
        lv_theme_apply(cb, LV_THEME_CHECKBOX);
        lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(cb, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_size(cb, LV_SIZE_AUTO, LV_SIZE_AUTO);
    }
    else {

        lv_checkbox_ext_t * copy_ext = lv_obj_get_ext_attr(copy);
        lv_style_list_copy(&ext->style_bullet, &copy_ext->style_bullet);

        /*Refresh the style with new signal function*/
        _lv_obj_refresh_style(cb, LV_OBJ_PART_ALL, LV_STYLE_PROP_ALL);
    }

    LV_LOG_INFO("check box created");

    return cb;
}

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the text of a check box. `txt` will be copied and may be deallocated
 * after this function returns.
 * @param cb pointer to a check box
 * @param txt the text of the check box. NULL to refresh with the current text.
 */
void lv_checkbox_set_text(lv_obj_t * cb, const char * txt)
{
    lv_checkbox_ext_t * ext = lv_obj_get_ext_attr(cb);
    size_t len = strlen(txt);

    if(!ext->static_txt) ext->txt = lv_mem_realloc(ext->txt, len + 1);
    else  ext->txt = lv_mem_alloc(len + 1);

    strcpy(ext->txt, txt);
    ext->static_txt = 0;

    _lv_obj_handle_self_size_chg(cb);
}

/**
 * Set the text of a check box. `txt` must not be deallocated during the life
 * of this checkbox.
 * @param cb pointer to a check box
 * @param txt the text of the check box. NULL to refresh with the current text.
 */
void lv_checkbox_set_text_static(lv_obj_t * cb, const char * txt)
{
    lv_checkbox_ext_t * ext = lv_obj_get_ext_attr(cb);

    if(!ext->static_txt) lv_mem_free(ext->txt);

    ext->txt = (char*)txt;
    ext->static_txt = 1;

    _lv_obj_handle_self_size_chg(cb);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Get the text of a check box
 * @param cb pointer to check box object
 * @return pointer to the text of the check box
 */
const char * lv_checkbox_get_text(const lv_obj_t * cb)
{
    lv_checkbox_ext_t * ext = lv_obj_get_ext_attr(cb);
    return ext->txt;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Handle the drawing related tasks of the check box
 * @param cb pointer to a check box object
 * @param clip_area the object will be drawn only in this area
 * @param mode LV_DESIGN_COVER_CHK: only check if the object fully covers the 'mask_p' area
 *                                  (return 'true' if yes)
 *             LV_DESIGN_DRAW: draw the object (always return 'true')
 *             LV_DESIGN_DRAW_POST: drawing after every children are drawn
 * @param return an element of `lv_design_res_t`
 */
static lv_design_res_t lv_checkbox_design(lv_obj_t * cb, const lv_area_t * clip_area, lv_design_mode_t mode)
{
    /* A label never covers an area */
    if(mode == LV_DESIGN_COVER_CHK)
        return LV_DESIGN_RES_NOT_COVER;
    else if(mode == LV_DESIGN_DRAW_MAIN) {
        /*Draw the background*/
        ancestor_design(cb, clip_area, mode);

        lv_checkbox_ext_t * ext = lv_obj_get_ext_attr(cb);

        const lv_font_t * font = lv_obj_get_style_text_font(cb, LV_CHECKBOX_PART_MAIN);
        lv_coord_t font_h = lv_font_get_line_height(font);

        lv_coord_t bg_topp = lv_obj_get_style_pad_top(cb, LV_CHECKBOX_PART_MAIN);
        lv_coord_t bg_leftp = lv_obj_get_style_pad_left(cb, LV_CHECKBOX_PART_MAIN);

        lv_coord_t bullet_leftm = lv_obj_get_style_margin_left(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_topm = lv_obj_get_style_margin_top(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_rightm = lv_obj_get_style_margin_right(cb, LV_CHECKBOX_PART_BULLET);

        lv_coord_t bullet_leftp = lv_obj_get_style_pad_left(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_rightp = lv_obj_get_style_pad_right(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_topp = lv_obj_get_style_pad_top(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_bottomp = lv_obj_get_style_pad_bottom(cb, LV_CHECKBOX_PART_BULLET);

        lv_draw_rect_dsc_t bullet_dsc;
        lv_draw_rect_dsc_init(&bullet_dsc);
        lv_obj_init_draw_rect_dsc(cb, LV_CHECKBOX_PART_BULLET, &bullet_dsc);
        lv_area_t bullet_area;
        bullet_area.x1 = cb->coords.x1 + bg_leftp + bullet_leftm;
        bullet_area.x2 = bullet_area.x1 + font_h + bullet_leftp + bullet_rightp - 1;
        bullet_area.y1 = cb->coords.y1 + bg_topp + bullet_topm;
        bullet_area.y2 = bullet_area.y1 + font_h + bullet_topp + bullet_bottomp - 1;

        lv_draw_rect(&bullet_area, clip_area, &bullet_dsc);

        lv_coord_t line_space = lv_obj_get_style_text_line_space(cb, LV_CHECKBOX_PART_MAIN);
        lv_coord_t letter_space = lv_obj_get_style_text_letter_space(cb, LV_CHECKBOX_PART_MAIN);

        lv_point_t txt_size;
        _lv_txt_get_size(&txt_size, ext->txt, font, letter_space, line_space, LV_COORD_MAX, LV_TXT_FLAG_NONE);

        lv_draw_label_dsc_t txt_dsc;
        lv_draw_label_dsc_init(&txt_dsc);
        lv_obj_init_draw_label_dsc(cb, LV_CHECKBOX_PART_MAIN, &txt_dsc);

        lv_coord_t y_ofs = (lv_area_get_height(&bullet_area) - font_h) / 2;
        lv_area_t txt_area;
        txt_area.x1 = bullet_area.x2 + bullet_rightm;
        txt_area.x2 = txt_area.x1 + txt_size.x;
        txt_area.y1 = cb->coords.y1 + bg_topp + y_ofs;
        txt_area.y2 = txt_area.y1 + txt_size.y;

        lv_draw_label(&txt_area, clip_area, &txt_dsc, ext->txt, NULL);

    } else {
        ancestor_design(cb, clip_area, mode);
    }

    return LV_DESIGN_RES_OK;
}
/**
 * Signal function of the check box
 * @param cb pointer to a check box object
 * @param sign a signal type from lv_signal_t enum
 * @param param pointer to a signal specific variable
 * @return LV_RES_OK: the object is not deleted in the function; LV_RES_INV: the object is deleted
 */
static lv_res_t lv_checkbox_signal(lv_obj_t * cb, lv_signal_t sign, void * param)
{
    lv_res_t res;
    /* Include the ancient signal function */
    res = ancestor_signal(cb, sign, param);
    if(res != LV_RES_OK) return res;

    if(sign == LV_SIGNAL_GET_STYLE) {
        lv_get_style_info_t * info = param;
        info->result = lv_checkbox_get_style(cb, info->part);
        if(info->result != NULL) return LV_RES_OK;
        else return ancestor_signal(cb, sign, param);
    }
    else if (sign == LV_SIGNAL_GET_TYPE) {
        return _lv_obj_handle_get_type_signal(param, LV_OBJX_NAME);
    }
    else if (sign == LV_SIGNAL_GET_SELF_SIZE) {
        lv_point_t * p = param;
        lv_checkbox_ext_t * ext = lv_obj_get_ext_attr(cb);

        const lv_font_t * font = lv_obj_get_style_text_font(cb, LV_CHECKBOX_PART_MAIN);
        lv_coord_t font_h = lv_font_get_line_height(font);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(cb, LV_CHECKBOX_PART_MAIN);
        lv_coord_t letter_space = lv_obj_get_style_text_letter_space(cb, LV_CHECKBOX_PART_MAIN);

        lv_point_t txt_size;
        _lv_txt_get_size(&txt_size, ext->txt, font, letter_space, line_space, LV_COORD_MAX, LV_TXT_FLAG_NONE);

        lv_coord_t bullet_leftm = lv_obj_get_style_margin_left(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_topm = lv_obj_get_style_margin_top(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_rightm = lv_obj_get_style_margin_right(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_bottomm = lv_obj_get_style_margin_bottom(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_leftp = lv_obj_get_style_pad_left(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_rightp = lv_obj_get_style_pad_right(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_topp = lv_obj_get_style_pad_top(cb, LV_CHECKBOX_PART_BULLET);
        lv_coord_t bullet_bottomp = lv_obj_get_style_pad_bottom(cb, LV_CHECKBOX_PART_BULLET);
        lv_point_t bullet_size;
        bullet_size.x = font_h + bullet_leftm + bullet_rightm + bullet_leftp + bullet_rightp;
        bullet_size.y = font_h + bullet_topm + bullet_bottomm + bullet_topp + bullet_bottomp;

        p->x = bullet_size.x + txt_size.x;
        p->y = LV_MATH_MAX(bullet_size.y, txt_size.y);

    }

    return res;
}


static lv_style_list_t * lv_checkbox_get_style(lv_obj_t * cb, uint8_t type)
{
    lv_style_list_t * style_dsc_p;

    lv_checkbox_ext_t * ext = lv_obj_get_ext_attr(cb);
    switch(type) {
        case LV_CHECKBOX_PART_MAIN:
            style_dsc_p = &cb->style_list;
            break;
        case LV_CHECKBOX_PART_BULLET:
            style_dsc_p = &ext->style_bullet;
            break;
        default:
            style_dsc_p = NULL;
    }

    return style_dsc_p;
}

#endif
