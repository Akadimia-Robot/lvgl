/**
 * @file lv_flex.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_flex.h"
#include "lv_obj.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_coord_t grow_unit;
    lv_coord_t track_cross_size;
    lv_coord_t track_main_size;
    uint32_t item_cnt;
}track_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
//static lv_obj_t * find_track_end(lv_obj_t * cont, lv_obj_t * item_start, lv_coord_t max_size, lv_coord_t * grow_unit, lv_coord_t * track_cross_size, lv_coord_t * track_main_size, uint32_t * item_cnt);
static lv_obj_t * find_track_end(lv_obj_t * cont, lv_obj_t * item_start, lv_coord_t max_main_size, track_t * t);
static void children_repos(lv_obj_t * cont, lv_obj_t * item_first, lv_obj_t * item_last, lv_coord_t abs_x, lv_coord_t abs_y, lv_coord_t max_main_size, lv_flex_place_t main_place, track_t * t);
static void place_content(lv_coord_t place, lv_coord_t max_size, lv_coord_t track_size, lv_coord_t item_cnt, lv_coord_t * start_pos, lv_coord_t * gap);

static lv_flex_dir_t get_dir(const lv_obj_t * obj);
static bool get_rev(const lv_obj_t * obj);
static bool get_wrap(const lv_obj_t * obj);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*=====================
 * Setter functions
 *====================*/

void lv_obj_set_flex_dir(lv_obj_t * obj, lv_flex_dir_t flex_dir)
{
    lv_obj_allocate_spec_attr(obj);

    if(obj->spec_attr->flex_cont.dir == flex_dir) return;

    obj->spec_attr->flex_cont.dir = flex_dir & 0x3;
    obj->spec_attr->flex_cont.wrap = flex_dir & _LV_FLEX_WRAP ? 1 : 0;
    obj->spec_attr->flex_cont.rev = flex_dir & _LV_FLEX_REVERSE ? 1 : 0;
    _lv_flex_refresh(obj);
}

void lv_obj_set_flex_place(lv_obj_t * obj, lv_flex_place_t main_place, lv_flex_place_t cross_place)
{
    lv_obj_allocate_spec_attr(obj);
    if(obj->spec_attr->flex_cont.main_place == main_place &&
       obj->spec_attr->flex_cont.cross_place == cross_place) {
        return;
    }

    obj->spec_attr->flex_cont.main_place = main_place;
    obj->spec_attr->flex_cont.cross_place = cross_place;

    _lv_flex_refresh(obj);
}

void lv_obj_set_flex_gap(lv_obj_t * obj, lv_coord_t gap)
{
    if(obj->spec_attr == NULL) lv_obj_allocate_spec_attr(obj);

    if(obj->spec_attr->flex_cont.gap == gap) return;

    obj->spec_attr->flex_cont.gap = gap;

    _lv_flex_refresh(obj);
}

void lv_obj_set_flex_item(lv_obj_t * obj, bool en)
{
    if(en) {
        lv_coord_t f  = _LV_COORD_FELX(LV_FLEX_PLACE_START);
        lv_obj_set_pos(obj, f, f);
    } else {
        lv_obj_set_pos(obj, lv_obj_get_x(obj), lv_obj_get_y(obj));
    }
}

void lv_obj_set_flex_item_place(lv_obj_t * obj, lv_flex_place_t place)
{
    if(place == LV_FLEX_PLACE_NONE) {
        lv_obj_set_pos(obj, lv_obj_get_x(obj), lv_obj_get_x(obj));
    } else {
        lv_coord_t f  = _LV_COORD_FELX(place);
        lv_obj_set_pos(obj, f, f);
    }
}

/*=====================
 * Getter functions
 *====================*/

lv_flex_dir_t lv_obj_get_flex_dir(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.dir;
    else return LV_FLEX_DIR_NONE;
}

lv_flex_place_t lv_obj_get_flex_item_place(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.main_place;
    else return LV_FLEX_PLACE_START;
}

lv_flex_place_t lv_obj_get_flex_track_place(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.cross_place;
    else return LV_FLEX_PLACE_START;
}

lv_coord_t lv_obj_get_flex_gap(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.gap;
    else return 0;
}

lv_flex_place_t lv_obj_get_flex_self_place(lv_obj_t * obj)
{
    lv_coord_t x = lv_obj_get_x(obj);
    if(LV_COORD_IS_FLEX(x)) return LV_COORD_GET_FLEX(x);
    else return LV_FLEX_PLACE_NONE;
}

void _lv_flex_refresh(lv_obj_t * cont)
{
    lv_flex_dir_t dir = get_dir(cont);

    if(dir == LV_FLEX_DIR_NONE) return;

    bool rtl = lv_obj_get_base_dir(cont) == LV_BIDI_DIR_RTL ? true : false;
    bool row = dir == LV_FLEX_DIR_ROW ? true : false;
    /*Count the grow units and free space*/
    lv_coord_t max_main_size = (row ? lv_obj_get_width_fit(cont) : lv_obj_get_height_fit(cont));
    lv_coord_t abs_y = cont->coords.y1 + lv_obj_get_style_pad_top(cont, LV_OBJ_PART_MAIN) - lv_obj_get_scroll_y(cont);
    lv_coord_t abs_x = cont->coords.x1 + lv_obj_get_style_pad_left(cont, LV_OBJ_PART_MAIN) - lv_obj_get_scroll_x(cont);

    lv_flex_place_t cross_place = lv_obj_get_flex_track_place(cont);
    lv_flex_place_t main_place = cont->spec_attr->flex_cont.main_place;
    lv_ll_t * ll = _lv_obj_get_child_ll(cont);
    lv_coord_t * cross_pos = (row ? &abs_y : &abs_x);

    if((row && cont->h_set == LV_SIZE_AUTO) ||
       (!row && cont->w_set == LV_SIZE_AUTO))
    {
        cross_place = LV_FLEX_PLACE_START;
    }

    if(rtl && !row) {
        if(cross_place == LV_FLEX_PLACE_START) cross_place = LV_FLEX_PLACE_END;
        else if(cross_place == LV_FLEX_PLACE_END) cross_place = LV_FLEX_PLACE_START;
    }

    lv_coord_t total_track_cross_size = 0;
    lv_coord_t gap = 0;
    uint32_t track_cnt = 0;
    lv_obj_t * track_first_item;
    lv_obj_t * next_track_first_item;
    bool rev = get_rev(cont);

    if(cross_place != LV_FLEX_PLACE_START) {
        track_first_item =  rev ? _lv_ll_get_head(ll) : _lv_ll_get_tail(ll);
        track_t t;
        while(track_first_item) {
            /*Search the first item of the next row */
            next_track_first_item = find_track_end(cont, track_first_item, max_main_size, &t);
            total_track_cross_size += t.track_cross_size;
            track_cnt++;
            track_first_item = next_track_first_item;
        }

        lv_coord_t max_cross_size = (row ? lv_obj_get_height_fit(cont) : lv_obj_get_width_fit(cont));
        place_content(cross_place, max_cross_size, total_track_cross_size, track_cnt, cross_pos, &gap);
    }

    track_first_item =  rev ? _lv_ll_get_head(ll) : _lv_ll_get_tail(ll);

    if(rtl && !row) {
         *cross_pos += total_track_cross_size;
    }

    while(track_first_item) {
        track_t t;
        /*Search the first item of the next row */
        next_track_first_item = find_track_end(cont, track_first_item, max_main_size, &t);

        if(rtl && !row) {
            *cross_pos -= t.track_cross_size;
        }
        children_repos(cont, track_first_item, next_track_first_item, abs_x, abs_y, max_main_size, main_place, &t);
        track_first_item = next_track_first_item;

        if(rtl && !row) {
            *cross_pos -=  gap;
        } else {
            *cross_pos += t.track_cross_size + gap;
        }
    }
    LV_ASSERT_MEM_INTEGRITY();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * find_track_end(lv_obj_t * cont, lv_obj_t * item_start, lv_coord_t max_main_size, track_t * t)
{
    bool wrap = get_wrap(cont);
    bool rev = get_rev(cont);
    lv_coord_t gap = lv_obj_get_flex_gap(cont);
    bool row = get_dir(cont) == LV_FLEX_DIR_ROW ? true : false;
    lv_coord_t(*get_main_size)(const lv_obj_t *) = (row ? lv_obj_get_width_margin : lv_obj_get_height_margin);
    lv_coord_t(*get_cross_size)(const lv_obj_t *) = (!row ? lv_obj_get_width_margin : lv_obj_get_height_margin);
    void * (*ll_iter)(const lv_ll_t * , const void *) = rev ? _lv_ll_get_next : _lv_ll_get_prev;

    lv_ll_t * ll = _lv_obj_get_child_ll(cont);

    lv_coord_t grow_sum = 0;
    t->track_main_size = 0;
    uint32_t grow_item_cnt = 0;
    t->track_cross_size = 0;
    t->grow_unit = 0;
    t->item_cnt = 0;

    lv_obj_t * item = item_start;
    while(item) {
        /*Ignore non-flex items*/
        lv_coord_t main_set = (row ? item->x_set : item->y_set);
        if(LV_COORD_IS_FLEX(main_set) == false) {
            item = ll_iter(ll, item);
            continue;
        }

        lv_coord_t main_size = (row ? item->w_set : item->h_set);
        if(_LV_FLEX_GET_GROW(main_size)) {
            grow_sum += _LV_FLEX_GET_GROW(main_size);
            grow_item_cnt++;
        } else {
            lv_coord_t item_size = get_main_size(item) + gap;
            if(wrap && t->track_main_size + item_size > max_main_size) break;
            t->track_main_size += item_size;
        }
        t->track_cross_size = LV_MATH_MAX(get_cross_size(item), t->track_cross_size);

        item = ll_iter(ll, item);
        t->item_cnt++;
    }

    if(t->track_main_size > 0) t->track_main_size -= gap; /*There is no gap after the last item*/

    if(grow_item_cnt && grow_sum) {
        lv_coord_t s = max_main_size - t->track_main_size;
        s -= grow_item_cnt * gap;
        t->grow_unit =  s / grow_sum;
        t->track_main_size = max_main_size;  /*If there is at least one "grow item" the track takes the full space*/
    } else {
        t->grow_unit = 0;
    }

    /*Have at least one item in a row*/
    if(item && item == item_start) {
        item = ll_iter(ll, item);
        if(item) {
            t->track_cross_size = get_cross_size(item);
            t->track_main_size = get_main_size(item);
            t->item_cnt = 1;
        }
    }

    return item;
}


static void children_repos(lv_obj_t * cont, lv_obj_t * item_first, lv_obj_t * item_last, lv_coord_t abs_x, lv_coord_t abs_y, lv_coord_t max_main_size, lv_flex_place_t main_place, track_t * t)
{
    bool rev = get_rev(cont);
    lv_coord_t gap = lv_obj_get_flex_gap(cont);
    bool row = get_dir(cont) == LV_FLEX_DIR_ROW ? true : false;

    lv_coord_t(*obj_get_main_size)(const lv_obj_t *) = (row ? lv_obj_get_width_margin : lv_obj_get_height_margin);
    lv_coord_t(*obj_get_cross_size)(const lv_obj_t *) = (!row ? lv_obj_get_width_margin : lv_obj_get_height_margin);
    void (*area_set_main_size)(lv_area_t *, lv_coord_t) = (row ? lv_area_set_width : lv_area_set_height);
    void (*area_set_cross_size)(lv_area_t *, lv_coord_t) = (!row ? lv_area_set_width : lv_area_set_height);
    lv_coord_t (*area_get_main_size)(const lv_area_t *) = (!row ? lv_area_get_width : lv_area_get_height);
    lv_style_int_t (*get_margin_start)(const lv_obj_t *, uint8_t part) = (row ? lv_obj_get_style_margin_left : lv_obj_get_style_margin_top);
    lv_style_int_t (*get_margin_end)(const lv_obj_t *, uint8_t part) = (row ? lv_obj_get_style_margin_right : lv_obj_get_style_margin_bottom);
    void * (*ll_iter)(const lv_ll_t * , const void *) = rev ? _lv_ll_get_next : _lv_ll_get_prev;

    bool rtl = lv_obj_get_base_dir(cont) == LV_BIDI_DIR_RTL ? true : false;

    if(row && rtl) abs_x += lv_obj_get_width_fit(cont);

    lv_ll_t * ll = _lv_obj_get_child_ll(cont);
    lv_coord_t main_pos = 0;

    lv_coord_t place_gap = 0;
    place_content(main_place, max_main_size, t->track_main_size, t->item_cnt, &main_pos, &place_gap);
    /*Reposition the children*/
    lv_obj_t * item = item_first;   /*Just to use a shorter name*/
    while(item != item_last) {

        /*Ignore non-flex items*/
        lv_coord_t main_set = (row ? item->x_set : item->y_set);
        if(LV_COORD_IS_FLEX(main_set) == false) {
            item = ll_iter(ll, item);
            continue;
        }

        lv_coord_t main_size = (row ? item->w_set : item->h_set);
        if(_LV_FLEX_GET_GROW(main_size) || LV_COORD_GET_FLEX(main_set) == LV_FLEX_PLACE_STRETCH) {
            lv_area_t old_coords;
            lv_area_copy(&old_coords, &item->coords);

            if(_LV_FLEX_GET_GROW(main_size)) {
                lv_coord_t s = _LV_FLEX_GET_GROW(main_size) * t->grow_unit;
                s -= get_margin_start(item, LV_OBJ_PART_MAIN) + get_margin_end(item, LV_OBJ_PART_MAIN);
                area_set_main_size(&item->coords, s);
            }
            if(LV_COORD_GET_FLEX(main_set) == LV_FLEX_PLACE_STRETCH) {
                area_set_cross_size(&item->coords, t->track_cross_size);
            }

            if(lv_area_get_height(&old_coords) != area_get_main_size(&item->coords)) {
                lv_obj_invalidate(item);
                item->signal_cb(item, LV_SIGNAL_COORD_CHG, &old_coords);
                lv_obj_invalidate(item);
            }
        }

        lv_coord_t cross_pos = 0;
        lv_coord_t cross_set = (row ? item->y_set : item->x_set);
        switch(LV_COORD_GET_FLEX(cross_set)) {
        case LV_FLEX_PLACE_CENTER:
            cross_pos = (t->track_cross_size - obj_get_cross_size(item)) / 2;
            break;
        case LV_FLEX_PLACE_END:
            cross_pos = t->track_cross_size - obj_get_cross_size(item);
            break;
        }

        if(row && rtl) {
            main_pos -= obj_get_main_size(item) + gap + place_gap;
        }

        lv_coord_t diff_x = abs_x - item->coords.x1 + lv_obj_get_style_margin_left(item, LV_OBJ_PART_MAIN);
        lv_coord_t diff_y = abs_y - item->coords.y1 + lv_obj_get_style_margin_top(item, LV_OBJ_PART_MAIN);
        diff_x += row ? main_pos : cross_pos;
        diff_y += row ? cross_pos : main_pos;

        if(diff_x || diff_y) {
            item->coords.x1 += diff_x;
            item->coords.x2 += diff_x;
            item->coords.y1 += diff_y;
            item->coords.y2 += diff_y;
            _lv_obj_move_children_by(item, diff_x, diff_y);
        }

        if(!(row && rtl)) {
            main_pos += obj_get_main_size(item) + gap + place_gap;
        }
        item = ll_iter(ll, item);
    }
}

static void place_content(lv_coord_t place, lv_coord_t max_size, lv_coord_t track_size, lv_coord_t item_cnt, lv_coord_t * start_pos, lv_coord_t * gap)
{
    if(item_cnt <= 1) {
        switch(place) {
            case LV_FLEX_PLACE_SPACE_BETWEEN:
            case LV_FLEX_PLACE_SPACE_AROUND:
            case LV_FLEX_PLACE_SPACE_EVENLY:
                place = LV_FLEX_PLACE_CENTER;
                break;
        }
    }

    switch(place) {
    case LV_FLEX_PLACE_CENTER:
        *gap = 0;
        *start_pos += (max_size - track_size) / 2;
        break;
    case LV_FLEX_PLACE_END:
        *gap = 0;
        *start_pos += max_size - track_size;
        break;
    case LV_FLEX_PLACE_SPACE_BETWEEN:
       *gap = (lv_coord_t)(max_size - track_size) / (lv_coord_t)(item_cnt - 1);
       break;
   case LV_FLEX_PLACE_SPACE_AROUND:
       *gap += (lv_coord_t)(max_size - track_size) / (lv_coord_t)(item_cnt);
       *start_pos += *gap / 2;
       break;
   case LV_FLEX_PLACE_SPACE_EVENLY:
       *gap = (lv_coord_t)(max_size - track_size) / (lv_coord_t)(item_cnt + 1);
       *start_pos += *gap;
       break;
   default:
       *gap = 0;
    }
}

static lv_flex_dir_t get_dir(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.dir;
    else return false;
}

static bool get_rev(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.rev;
    else return false;
}

static bool get_wrap(const lv_obj_t * obj)
{
    if(obj->spec_attr) return obj->spec_attr->flex_cont.wrap;
    else return false;
}
