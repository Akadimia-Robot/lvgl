/**
 * @file lv_img.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_img_src.h"
#include "lv_img_buf.h"

#include "../misc/lv_assert.h"

static lv_res_t alloc_str_src(lv_img_src_t * src, const char * str);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Get the type of an image source
 * @param src pointer to an image source:
 *  - pointer to an 'lv_img_t' variable (image stored internally and compiled into the code)
 *  - a path to a file (e.g. "S:/folder/image.bin")
 *  - or a symbol (e.g. LV_SYMBOL_CLOSE)
 * @return type of the image source LV_IMG_SRC_VARIABLE/FILE/SYMBOL/UNKNOWN
 * @deprecated You shouldn't rely on this function to find out the image type
 */
lv_img_src_type_t lv_img_src_get_type(const void * src)
{
    lv_img_src_type_t img_src_type = LV_IMG_SRC_UNKNOWN;

    if(src == NULL) return img_src_type;
    const uint8_t * u8_p = src;

    /*The first byte shows the type of the image source*/
    if(u8_p[0] >= 0x20 && u8_p[0] <= 0x7F) {
        img_src_type = LV_IMG_SRC_FILE; /*If it's an ASCII character then it's file name*/
    }
    else if(u8_p[0] >= 0x80) {
        img_src_type = LV_IMG_SRC_SYMBOL; /*Symbols begins after 0x7F*/
    }
    else {
        img_src_type = LV_IMG_SRC_VARIABLE; /*`lv_img_dsc_t` is draw to the first byte < 0x20*/
    }

    if(LV_IMG_SRC_UNKNOWN == img_src_type) {
        LV_LOG_WARN("lv_img_src_get_type: unknown image type");
    }

    return img_src_type;
}

lv_res_t lv_img_src_parse(lv_img_src_t * uri, const void * src)
{
    lv_img_src_type_t src_type = lv_img_src_get_type(src);

    switch(src_type) {
        case LV_IMG_SRC_FILE:
#if LV_USE_LOG && LV_LOG_LEVEL >= LV_LOG_LEVEL_INFO
            LV_LOG_TRACE("lv_img_src_parse: `LV_IMG_SRC_FILE` type found");
#endif
            lv_img_src_set_file(uri, src);
            break;
        case LV_IMG_SRC_VARIABLE: {
#if LV_USE_LOG && LV_LOG_LEVEL >= LV_LOG_LEVEL_INFO
                LV_LOG_TRACE("lv_img_src_parse: `LV_IMG_SRC_VARIABLE` type found");
#endif
                lv_img_dsc_t * id = (lv_img_dsc_t *) src; /*This might break if given any raw data here*/
                lv_img_src_set_data(uri, (const uint8_t *)src, id->data_size);
            }
            break;
        case LV_IMG_SRC_SYMBOL:
#if LV_USE_LOG && LV_LOG_LEVEL >= LV_LOG_LEVEL_INFO
            LV_LOG_TRACE("lv_img_src_parse: `LV_IMG_SRC_SYMBOL` type found");
#endif
            lv_img_src_set_symbol(uri, src);
            break;
        default:
            LV_LOG_WARN("lv_img_src_parse: unknown image type");
            lv_img_src_free(uri);
            return LV_RES_INV;
    }
    return LV_RES_OK;
}


/** Free a source descriptor.
 *  Only to be called if allocated via lv_img_src_parse
 *  @param src  The src format to free
 */
void lv_img_src_free(lv_img_src_t * src)
{
    if(src->type == LV_IMG_SRC_SYMBOL || src->type == LV_IMG_SRC_FILE) {
        lv_mem_free((void *)src->uri);
    }
    lv_memset_00(src, sizeof(*src));
}

void lv_img_src_set_file(lv_img_src_t * obj, const char * file_path)
{
    lv_img_src_free(obj);

    obj->type = LV_IMG_SRC_FILE;
    if(alloc_str_src(obj, file_path) == LV_RES_INV)
        return;

    obj->ext = strrchr(obj->uri, '.');
}

void lv_img_src_set_data(lv_img_src_t * obj, const uint8_t * data, const size_t len)
{
    lv_img_src_free(obj);

    obj->type = LV_IMG_SRC_VARIABLE;
    obj->uri = data;
    obj->uri_len = len;
}

void lv_img_src_set_symbol(lv_img_src_t * obj, const char * symbol)
{
    lv_img_src_free(obj);

    obj->type = LV_IMG_SRC_SYMBOL;
    if(alloc_str_src(obj, symbol) == LV_RES_INV)
        return;
}

void lv_img_src_copy(lv_img_src_t * dest, const lv_img_src_t * src)
{
    lv_img_src_free(dest);
    dest->type = LV_IMG_SRC_UNKNOWN;
    dest->uri = src->uri;
    dest->uri_len = src->uri_len;
    dest->ext = NULL;
    if(src->type != LV_IMG_SRC_VARIABLE && alloc_str_src(dest, (const char *)src->uri) == LV_RES_INV) {
        return;
    }
    dest->type = src->type;
    if(src->type == LV_IMG_SRC_FILE) {
        dest->ext = strrchr(dest->uri, '.');
    }
}


static lv_res_t alloc_str_src(lv_img_src_t * src, const char * str)
{
    src->uri_len = strlen(str);
    src->uri = lv_mem_alloc(src->uri_len + 1);
    LV_ASSERT_MALLOC(src->uri);
    if(src->uri == NULL) {
        src->uri_len = 0;
        return LV_RES_INV;
    }

    lv_memcpy(src->uri, str, src->uri_len + 1);
    return LV_RES_OK;
}
