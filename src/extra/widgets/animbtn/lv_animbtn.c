/**
 * @file lv_animbtn.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_animbtn.h"

#if LV_USE_ANIMBTN != 0

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_animbtn_class

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_animbtn_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_animbtn_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_animbtn_event(const lv_obj_class_t * class_p, lv_event_t * e);
static void apply_state(lv_obj_t * animbtn, bool skip_transition);
static void loop_state(lv_obj_t * animbtn);
static lv_animbtn_state_t suggest_state(lv_obj_t * animbtn, lv_animbtn_state_t state);
static lv_animbtn_state_t get_state(const lv_obj_t * animbtn);
static void setup_anim(lv_animbtn_t * animbtn, lv_animbtn_state_desc_t * desc);
static int is_state_valid(const lv_animbtn_state_desc_t * state);
static uint8_t find_trans(lv_animbtn_t * animbtn, lv_animbtn_state_t from, lv_animbtn_state_t to);
static bool is_transiting(lv_animbtn_t * animbtn, lv_animbtn_state_t current_state);

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_animbtn_class = {
    .base_class = &lv_obj_class,
    .instance_size = sizeof(lv_animbtn_t),
    .constructor_cb = lv_animbtn_constructor,
    .destructor_cb = lv_animbtn_destructor,
    .event_cb = lv_animbtn_event,
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create an image button object
 * @param parent pointer to an object, it will be the parent of the new image button
 * @return pointer to the created image button
 */
lv_obj_t * lv_animbtn_create(lv_obj_t * parent, lv_obj_t * anim)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    /*Capture the animation picture*/
    ((lv_animbtn_t *)obj)->img = anim;
    lv_obj_set_parent(anim, obj);
    lv_obj_add_flag(anim, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_img_t * img = (lv_img_t *)anim;
    lv_obj_set_size(obj, img->w, img->h);
    return obj;
}

/*=====================
 * Setter functions
 *====================*/

/**
 * Set images for a state of the image button
 * @param obj pointer to an image button object
 * @param state for which state set the new image
 * @param src_left pointer to an image source for the left side of the button (a C array or path to
 * a file)
 * @param src_mid pointer to an image source for the middle of the button (ideally 1px wide) (a C
 * array or path to a file)
 * @param src_right pointer to an image source for the right side of the button (a C array or path
 * to a file)
 */
void lv_animbtn_set_state_desc(lv_obj_t * obj, lv_animbtn_state_t state, const lv_animbtn_state_desc_t desc)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;

    animbtn->state_desc[state - 1] = desc;
    animbtn->state_desc[state - 1].control |=
        LV_IMG_CTRL_MARKED; /*A non existant flag used to mark that the state was used*/
    apply_state(obj, false);
}

void lv_animbtn_set_transition_desc(lv_obj_t * obj, lv_animbtn_state_t from_state, lv_animbtn_state_t to_state,
                                    lv_animbtn_state_desc_t desc)
{
    if(LV_BT(desc.control, LV_ANIMBTN_CTRL_LOOP)) {
        return; /* Loop not allowed in transition */
    }

    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    uint8_t pos = find_trans(animbtn, from_state, to_state);
    if(pos != animbtn->trans_count) {
        animbtn->trans_desc[pos].desc = desc;
        return;
    }
    /* Allocate a transition array now */
    animbtn->trans_desc = (lv_animbtn_transition_t *)lv_mem_realloc(animbtn->trans_desc,
                                                                    (animbtn->trans_count + 1) * sizeof(*animbtn->trans_desc));
    LV_ASSERT_MALLOC(animbtn->trans_desc);
    animbtn->trans_desc[animbtn->trans_count].from = from_state;
    animbtn->trans_desc[animbtn->trans_count].to = to_state;
    animbtn->trans_desc[animbtn->trans_count].desc = desc;
    animbtn->trans_count++;
}


void lv_animbtn_set_state(lv_obj_t * obj, lv_animbtn_state_t state, bool skip_transition)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_state_t obj_state = LV_STATE_DEFAULT;
    if(state == LV_ANIMBTN_STATE_PRESSED || state == LV_ANIMBTN_STATE_CHECKED_PRESSED) obj_state |= LV_STATE_PRESSED;
    if(state == LV_ANIMBTN_STATE_DISABLED || state == LV_ANIMBTN_STATE_CHECKED_DISABLED) obj_state |= LV_STATE_DISABLED;
    if(state == LV_ANIMBTN_STATE_CHECKED_DISABLED || state == LV_ANIMBTN_STATE_CHECKED_PRESSED ||
       state == LV_ANIMBTN_STATE_CHECKED_RELEASED) {
        obj_state |= LV_STATE_CHECKED;
    }

    lv_obj_clear_state(obj, LV_STATE_CHECKED | LV_STATE_PRESSED | LV_STATE_DISABLED);
    lv_obj_add_state(obj, obj_state);

    apply_state(obj, skip_transition);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Get the right image in a given state
 * @param obj pointer to an image button object
 * @param state the state where to get the image (from `lv_btn_state_t`) `
 * @return pointer to the left image source (a C array or path to a file)
 */
const lv_animbtn_state_desc_t * lv_animbtn_get_state_desc(lv_obj_t * obj, lv_animbtn_state_t state)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;

    return &animbtn->state_desc[state - 1];
}


/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint8_t find_trans(lv_animbtn_t * animbtn, lv_animbtn_state_t from, lv_animbtn_state_t to)
{
    uint8_t i = 0;
    for(; i < animbtn->trans_count; i++) {
        if(animbtn->trans_desc[i].from == from && animbtn->trans_desc[i].to == to) {
            return i;
        }
    }
    return i;
}

static bool is_transiting(lv_animbtn_t * animbtn, lv_animbtn_state_t current_state)
{
    return animbtn->prev_state != current_state;
}


static void lv_animbtn_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    /*Initialize the allocated 'ext'*/
    lv_memset_00(animbtn->state_desc, sizeof(animbtn->state_desc));
    animbtn->img = NULL;
    animbtn->prev_state = 0;
    animbtn->trans_count = 0;
    animbtn->trans_desc = 0;

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);
}

static void lv_animbtn_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    lv_mem_free(animbtn->trans_desc);
    animbtn->trans_desc = 0;
    animbtn->trans_count = 0;
}


static void lv_animbtn_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_res_t res = lv_obj_event_base(&lv_animbtn_class, e);
    if(res != LV_RES_OK) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_current_target(e);
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    switch(code) {
        case LV_EVENT_READY:
            /*Should we loop?*/
            loop_state(obj);
            break;
        case LV_EVENT_PRESSED:
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            apply_state(obj, false);
            break;
        case LV_EVENT_COVER_CHECK: {
                lv_cover_check_info_t * info = lv_event_get_param(e);
                if(info->res != LV_COVER_RES_MASKED) info->res = LV_COVER_RES_NOT_COVER;
                break;
            }
        case LV_EVENT_GET_SELF_SIZE: {
                lv_point_t * p = lv_event_get_self_size_info(e);
                p->x = LV_MAX(p->x, ((lv_img_t *)animbtn->img)->w);
                break;
            }
        default:
            break;
    }
}

static void setup_anim(lv_animbtn_t * animbtn, lv_animbtn_state_desc_t * desc)
{
    /*Set the logic for the current state*/
    int backward = LV_BT(desc->control, LV_IMG_CTRL_BACKWARD);
    if(backward && desc->first_frame < desc->last_frame) {
        /*Play in reverse means start from last to first*/
        lv_img_set_current_frame(animbtn->img, desc->last_frame);
        lv_img_set_stop_at_frame(animbtn->img, desc->first_frame, !backward);
    }
    else {
        lv_img_set_current_frame(animbtn->img, desc->first_frame);
        lv_img_set_stop_at_frame(animbtn->img, desc->last_frame, !backward);
    }
}

static void loop_state(lv_obj_t * obj)
{
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    lv_animbtn_state_t current_state = get_state(obj);
    lv_animbtn_state_t state  = suggest_state(obj, current_state);
    if(is_transiting(animbtn, current_state)) {
        /* Need to end transition here and switch to state animation */
        animbtn->prev_state = state;
    }

    if(animbtn->prev_state != state || animbtn->img == NULL || !is_state_valid(&animbtn->state_desc[state - 1])) return;

    /*Set the logic for the current state*/
    if(LV_BT(animbtn->state_desc[state - 1].control, LV_IMG_CTRL_LOOP)) {
        setup_anim(animbtn, &animbtn->state_desc[state - 1]);
    }
}


static void apply_state(lv_obj_t * obj, bool skip_transition)
{
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    lv_animbtn_state_t current_state = get_state(obj);
    lv_animbtn_state_t state  = suggest_state(obj, current_state);
    if(is_transiting(animbtn, current_state) && !skip_transition) {
        /* We are transiting now */
        uint8_t pos = find_trans(animbtn, animbtn->prev_state, current_state);
        if(pos != animbtn->trans_count) {
            setup_anim(animbtn, &animbtn->trans_desc[pos].desc);

            lv_obj_refresh_self_size(obj);
            lv_obj_invalidate(obj);
            return;
        }
    }

    if(state == animbtn->prev_state || animbtn->img == NULL || !is_state_valid(&animbtn->state_desc[state - 1])) return;

    /*Set the logic for the current state*/
    setup_anim(animbtn, &animbtn->state_desc[state - 1]);

    lv_obj_refresh_self_size(obj);

    lv_obj_invalidate(obj);
    animbtn->prev_state = state;
}

/**
 * Check if a state is valid (initialized).
 */
static int is_state_valid(const lv_animbtn_state_desc_t * state)
{
    return LV_BT(state->control, LV_IMG_CTRL_MARKED);
}


/**
 * If `src` is not defined for the current state try to get a state which is related to the current but has a valid descriptor.
 * E.g. if the PRESSED src is not set but the RELEASED does, use the RELEASED.
 * @param animbtn pointer to an image button
 * @param state the state to convert
 * @return the suggested state
 */
static lv_animbtn_state_t suggest_state(lv_obj_t * obj, lv_animbtn_state_t state)
{
    lv_animbtn_t * animbtn = (lv_animbtn_t *)obj;
    if(!is_state_valid(&animbtn->state_desc[state - 1])) {
        switch(state) {
            case LV_ANIMBTN_STATE_PRESSED:
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_RELEASED - 1])) return LV_ANIMBTN_STATE_RELEASED;
                break;
            case LV_ANIMBTN_STATE_CHECKED_RELEASED:
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_RELEASED - 1])) return LV_ANIMBTN_STATE_RELEASED;
                break;
            case LV_ANIMBTN_STATE_CHECKED_PRESSED:
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_CHECKED_RELEASED - 1])) return
                        LV_ANIMBTN_STATE_CHECKED_RELEASED;
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_PRESSED - 1])) return LV_ANIMBTN_STATE_PRESSED;
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_RELEASED - 1])) return LV_ANIMBTN_STATE_RELEASED;
                break;
            case LV_ANIMBTN_STATE_DISABLED:
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_RELEASED - 1])) return LV_ANIMBTN_STATE_RELEASED;
                break;
            case LV_ANIMBTN_STATE_CHECKED_DISABLED:
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_CHECKED_RELEASED - 1])) return
                        LV_ANIMBTN_STATE_CHECKED_RELEASED;
                if(is_state_valid(&animbtn->state_desc[LV_ANIMBTN_STATE_RELEASED - 1])) return LV_ANIMBTN_STATE_RELEASED;
                break;
            default:
                break;
        }
    }

    return state;
}

lv_animbtn_state_t get_state(const lv_obj_t * animbtn)
{
    LV_ASSERT_OBJ(animbtn, MY_CLASS);

    lv_state_t obj_state = lv_obj_get_state(animbtn);

    if(obj_state & LV_STATE_DISABLED) {
        if(obj_state & LV_STATE_CHECKED) return LV_ANIMBTN_STATE_CHECKED_DISABLED;
        else return LV_ANIMBTN_STATE_DISABLED;
    }

    if(obj_state & LV_STATE_CHECKED) {
        if(obj_state & LV_STATE_PRESSED) return LV_ANIMBTN_STATE_CHECKED_PRESSED;
        else return LV_ANIMBTN_STATE_CHECKED_RELEASED;
    }
    else {
        if(obj_state & LV_STATE_PRESSED) return LV_ANIMBTN_STATE_PRESSED;
        else return LV_ANIMBTN_STATE_RELEASED;
    }
}

#endif
