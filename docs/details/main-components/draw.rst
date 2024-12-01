.. _draw:

================
Drawing Pipeline
================

LVGL has a flexible and extendable drawing pipeline.  You can hook it to do
some rendering with a GPU or even completely replace the built-in
software renderer.

By using events, it's also possible to modify `draw_tasks` on insert new ones as LVGL renders the widgets.


Overview
********

The followings describe the basics terminology and concept of rendering.

Draw task
---------


When :cpp:expr:`lv_draw_rect`, :cpp:expr:`lv_draw_label` or similar functions are called
LVGL creates a so called draw task.

Draw unit
---------

The draw tasks are collected in a list and periodically dispatched to draw units. A
draw unit can a CPU core, a GPU, just a new rendering library for certain or all draw tasks,
or basically anything that can draw somehow.

Draw task evaluation
--------------------

Different draw units might render slight different output for example for an image transformation or
a gradient. If such a draw task were assigned to a different draw units, the screen might jitter a
little bit. To resolve it each draw unit has an ``evaluate_cb`` which is called when a draw task is created.
Based on the type and parameters of the draw task each draw unit can decide if it want to assign the
draw task to itself. This way a certain type of draw task (e.g. rounded rectangle with horizontal
gradient) will be always assigned to the same draw unit. It avoid the above mentioned issue of
slight difference between draw units.


Dispatching
-----------

While collecting draw tasks LVGL frequently tries to dispatch the collected draw tasks to the draw units.
This handles via the ``dispatch_cb`` of the draw units.

If a draw unit is busy with another draw task, it just returns. However, it is available it can take a draw task.

:cpp:expr:`lv_draw_get_next_available_task(layer, previous_task, draw_unit_id)` is a useful helper function which
returns an available draw task. "Available draw task" means that, all the draw tasks which should be drawn under a draw task
are ready and it is assigned to the given draw unit.


Layers
------

A layer is a buffer with a given area on which rendering happens. Each display has a "main" layer, but
during rendering additional layers might be created internally to handle for example widget transformations.


Hierarchy of modules
--------------------

All these together looks like this

- list of draw units
- display(s): Each display has a main layer and other layer might be create during rendering

   - layer(s): Each display has its own list of layers

      - draw tasks: Each layer has its own list of draw tasks

Custom Drawing
**************

Draw Events
-----------

LVGL provides two ways to customize the rendering of any widgets:
- Allow adding custom ``draw_task``s at various stages of rendering. The related event codes are:

  - ``LV_EVENT_DRAW_MAIN_BEGIN``, ``LV_EVENT_DRAW_MAIN``, ``LV_EVENT_DRAW_MAIN_END``:
    Called before, during, or after the widget is drawn. The built-in widget rendering usually happens in ``LV_EVENT_DRAW_MAIN``.
  - ``LV_EVENT_DRAW_POST_BEGIN``, ``LV_EVENT_DRAW_POST``, ``LV_EVENT_DRAW_POST_END``:
    Called before, during, or after all the children are rendered to add some overlay-like drawings. For example, scrollbars are rendered here.

- Modify a ``draw_task`` created by a widget. For this, ``LV_EVENT_DRAW_TASK_ADDED`` can be used.
  For performance reasons, this event is disabled by default and can be enabled by setting the
  ``LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS`` flag.
  A use case for this event is when you want to modify each bar of a bar chart.

Draw Descriptors
----------------

The ``draw_task``s are created internally when an ``lv_draw_rect/label/image/etc`` function is called.
These functions have the following parameters:
- an ``lv_layer_t`` parameter (can be obtained in the ``LV_EVENT_DRAW_MAIN/POST_...`` events by ``lv_event_get_layer(e)``)
- a draw descriptor, which is a large ``struct`` containing all the information about the drawing
- an area, in some cases, to specify where to draw

Each draw task type has its own draw descriptor. For example, ``lv_draw_label_dsc_t`` for label drawing.

Before using a draw descriptor in a draw event, it needs to be initialized with the related function.
For example, ``lv_draw_label_dsc_init(&my_label_draw_dsc);``

After that, each field of the draw descriptor can be set.
The default values are quite reasonable, so usually only a few fields need to be modified. For example:

.. code-block:: c
   /*In LV_EVENT_DRAW_MAIN*/

   lv_draw_label_dsc_t my_label_draw_dsc;
   lv_draw_label_dsc_init(&my_label_draw_dsc);
   my_label_draw_dsc.font = &my_font;
   my_label_draw_dsc.color = lv_color_hex(0xff0000);
   my_label_draw_dsc.text = "Hello";

   lv_area_t a = {10, 10, 200, 50}; /*Draw the label here*/

   lv_draw_label(lv_event_get_layer(e), &my_label_draw_dsc, &a);

When rendering a part of the widget, helper functions can be used to initialize the draw descriptors
based on the state, styles, and a given part of a widget. For example:

.. code-block:: c
   /*In LV_EVENT_DRAW_MAIN*/
    lv_draw_rect_dsc_t cur_dsc;
    lv_draw_rect_dsc_init(&cur_dsc);
    lv_obj_init_draw_rect_dsc(obj, LV_PART_CURSOR, &cur_dsc);
    cur_dsc.fill_color = lv_color_hex(0xff0000); /*Modify if needed*/
    lv_draw_rect(layer, &cur_dsc, &area);

When an ``lv_draw_...`` function is called, besides creating a draw task, it also copies
the draw descriptor and frees it automatically when needed. Therefore, it's fine to use
local draw descriptor variables.

In ``LV_EVENT_DRAW_TASK_ADDED`` the draw descriptor of the ``draw_task`` can be accessed
(using, for example, ``lv_draw_task_get_label_dsc()``) and modified (e.g., change the color, text, font, etc.).
It also means that in ``LV_EVENT_DRAW_TASK_ADDED`` the ``draw_task``s and draw descriptors are already
initialized. In practice, it looks like this:

.. code-block:: c
   /*In LV_EVENT_DRAW_TASK_ADDED*/
   lv_draw_task_t * t = lv_event_get_draw_task(e);
   lv_draw_label_dsc_t * draw_dsc =  lv_draw_task_get_label_dsc(t);
   /*Make the color lighter for longer texts*/
   draw_dsc->color = lv_color_lighten(draw_dsc->color, LV_MIN(lv_strlen(draw_dsc->text) * 5, 255));

   /*Create new draw tasks if needed here by calling lv_draw_...() functions*/

In the following, a detailed description of each draw task and descriptor type will be shown.

Relation to styles
------------------

In most of the cases the style properties are 1-1 mapped to the draw descriptor fields.
For example ``label_dsc.color`` is the ``text_color`` style property.
Or ``shadow_dsc.width``, ``line_dsc.opa``, ``arc_dsc.width`` are
``shadow_width``, ``line_opa``, and `arc_width` in case of the styles.

Therefor on this page only the differences and special considerations are highlighted.
For a detailed description on the meaning of each field see

- :ref:`style_properties`
- Style examples
- Canvas examples


Base Draw Descriptor
--------------------

Each draw descriptor has a generic "base descriptor" of type ``lv_draw_dsc_base_t`` with the name ``base``. It stores useful information about which widget and part created the draw descriptor.
See all the fields in :cpp:expr:`lv_draw_dsc_base_t`.

The ``lv_obj_init_draw_...`` functions automatically initialize the fields of the base descriptor.

Rectangle Draw Descriptor
-------------------------

The :cpp:expr:`lv_draw_rect_dsc_t` is a helper descriptor that combines:

- fill
- border
- outline (essentially a border but with its own styles)
- shadow
- background image (an image with its own styles)

into one call.

``lv_obj_init_draw_rect_dsc(obj, part, &dsc);`` can be used to initialize a draw descriptor from a widget,
and ``lv_draw_rect(layer, &dsc, area)`` draws the given rectangle in an area.

Fill Draw Descriptor
--------------------

The :cpp:expr:`lv_draw_fill_dsc_t` fill descriptor is straightforward.
It has a radius, opacity, and color to draw a rectangle.

If the opacity is 0, no draw task will be created.

- ``lv_draw_fill_dsc_init(&dsc)`` initializes a fill draw task.
- ``lv_draw_fill(layer, &dsc, area)`` creates a draw task to fill an area.
- ``lv_draw_task_get_fill_dsc(draw_task)`` gets the fill descriptor from a draw task.

Gradients
^^^^^^^^^

The ``grad`` field of the fill descriptor (or :cpp_expression:`lv_grad_dsc_t` in general) can describe:
- horizontal
- vertical
- skew
- radial
- conic

gradients with multiple stops.

TODO: Describe each in detail here.

Border Draw Descriptor
-----------------------

Box Shadow Draw Descriptor
---------------------------

Image Draw Descriptor
----------------------

Layers - Special Images
^^^^^^^^^^^^^^^^^^^^^^^

Label Draw Descriptor
---------------------

Arc Draw Descriptor
--------------------

Triangle Draw Descriptor
-------------------------

Mask Draw Descriptor
--------------------

Rectangle Mask
^^^^^^^^^^^^^^

Bitmap Mask
^^^^^^^^^^^

Vector Draw Descriptor
-----------------------


References
**********

As a reference take a look at `lv_draw_sw.c <https://github.com/lvgl/lvgl/blob/master/src/draw/sw/lv_draw_sw.c>`__

API
***

