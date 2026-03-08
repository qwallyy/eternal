/**
 * C++ compatible wrapper for wlr_scene.h.
 *
 * wlr_scene.h uses C11 [static 4] array syntax which is invalid in C++.
 * This header can be included from C++ code inside an extern "C" block.
 * It provides all types and most functions from wlr_scene.h, but replaces
 * the two problematic function declarations with C++-compatible signatures.
 */
#ifndef ETERNAL_WLR_SCENE_COMPAT_H
#define ETERNAL_WLR_SCENE_COMPAT_H

#ifdef __cplusplus
/* In C++ mode, we cannot include wlr_scene.h directly because of [static 4].
 * Instead, provide the two problematic declarations with plain [4]. */

/* First define the include guard so the real header is never pulled in */
#define WLR_TYPES_WLR_SCENE_H

/* Include headers that wlr_scene.h depends on */
#include <wayland-server-core.h>
#include <pixman.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/addon.h>
#include <wlr/render/pass.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/render/wlr_renderer.h>
#include <time.h>

/* Minimal forward declarations */
struct wlr_buffer;
struct wlr_xdg_surface;
struct wlr_layer_surface_v1;
struct wlr_drag_icon;
struct wlr_surface;
struct wlr_linux_dmabuf_feedback_v1;
struct wlr_linux_dmabuf_feedback_v1_init_options;
struct wlr_presentation;
struct wlr_scene_output_layout;
struct wlr_color_transform;
struct wlr_swapchain;
struct wlr_fbox;

/* wlr_scale_filter_mode is defined in wlr/render/pass.h (included above) */

typedef void (*wlr_scene_buffer_iterator_func_t)(
    struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);

enum wlr_scene_node_type {
    WLR_SCENE_NODE_TREE,
    WLR_SCENE_NODE_RECT,
    WLR_SCENE_NODE_BUFFER,
};

struct wlr_scene_node {
    enum wlr_scene_node_type type;
    struct wlr_scene_tree *parent;
    struct wl_list link;
    bool enabled;
    int x, y;
    struct {
        struct wl_signal destroy;
    } events;
    void *data;
    struct wlr_addon_set addons;
    pixman_region32_t visible;
};

struct wlr_scene_tree {
    struct wlr_scene_node node;
    struct wl_list children;
};

struct wlr_scene {
    struct wlr_scene_tree tree;
    struct wl_list outputs;
    struct wl_listener linux_dmabuf_v1_destroy;
    struct {
        struct wl_signal output_layout_changed;
    } events;
    bool calculate_visibility;
    bool direct_scanout;
    bool highlight_transparent_region;
    bool shadow_data_changed;
};

struct wlr_scene_surface {
    struct wlr_scene_buffer *buffer;
    struct wlr_surface *surface;
    struct wl_listener outputs_update;
    struct wl_listener output_enter;
    struct wl_listener output_leave;
    struct wl_listener output_sample;
    struct wl_listener frame_done;
    struct wl_listener surface_destroy;
    struct wl_listener surface_commit;
};

struct wlr_scene_rect {
    struct wlr_scene_node node;
    int width, height;
    float color[4];
};

struct wlr_scene_buffer {
    struct wlr_scene_node node;
    struct wlr_buffer *buffer;
    struct wlr_fbox src_box;
    int dst_width, dst_height;
    enum wl_output_transform transform;
    pixman_region32_t opaque_region;
    uint64_t active_outputs;
    struct wlr_texture *texture;
    struct wlr_texture *own_texture;
    bool shadow_data_changed;
    struct wlr_linux_dmabuf_feedback_v1 *primary_output_dmabuf_feedback;
    uint32_t primary_output_format_set_count;
    struct wlr_scene_output *primary_output;
    float opacity;
    enum wlr_scale_filter_mode filter_mode;
    struct {
        struct wl_signal outputs_update;
        struct wl_signal output_enter;
        struct wl_signal output_leave;
        struct wl_signal output_sample;
        struct wl_signal frame_done;
    } events;
    struct wl_listener buffer_release;
    struct wlr_addon renderer_addon;
    struct wlr_scene_output *primary_output_committed;
    pixman_region32_t opaque_region_committed;
};

struct wlr_scene_output_state_options {
    struct wlr_scene_timer *timer;
    struct wlr_color_transform *color_transform;
    struct wlr_swapchain *swapchain;
};

struct wlr_scene_output {
    struct wlr_scene_output_state_options pending;
    struct wlr_output *output;
    struct wl_list link;
    struct wlr_scene *scene;
    struct wlr_addon addon;
    struct wlr_damage_ring damage_ring;
    int x, y;
    struct {
        struct wl_signal destroy;
    } events;
    uint8_t index;
    bool prev_scanout;
    struct wl_listener output_commit;
    struct wl_listener output_damage;
    struct wl_listener output_needs_frame;
};

struct wlr_scene_timer {
    int64_t pre_render_duration;
    int64_t render_timer_duration;
    struct wlr_render_timer *render_timer;
};

/* Function declarations - compatible with C++ */
extern "C" {

struct wlr_scene *wlr_scene_create(void);

struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *parent);
void wlr_scene_node_destroy(struct wlr_scene_node *node);
void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled);
void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y);
void wlr_scene_node_place_above(struct wlr_scene_node *node,
    struct wlr_scene_node *sibling);
void wlr_scene_node_place_below(struct wlr_scene_node *node,
    struct wlr_scene_node *sibling);
void wlr_scene_node_raise_to_top(struct wlr_scene_node *node);
void wlr_scene_node_lower_to_bottom(struct wlr_scene_node *node);
void wlr_scene_node_reparent(struct wlr_scene_node *node,
    struct wlr_scene_tree *new_parent);
bool wlr_scene_node_coords(struct wlr_scene_node *node, int *lx, int *ly);
void wlr_scene_node_for_each_buffer(struct wlr_scene_node *node,
    wlr_scene_buffer_iterator_func_t iterator, void *user_data);
struct wlr_scene_node *wlr_scene_node_at(struct wlr_scene_node *node,
    double lx, double ly, double *nx, double *ny);

struct wlr_scene_surface *wlr_scene_surface_try_from_buffer(
    struct wlr_scene_buffer *scene_buffer);
struct wlr_scene_buffer *wlr_scene_buffer_from_node(
    struct wlr_scene_node *node);

/* C++-compatible versions of the [static 4] functions */
struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_tree *parent,
    int width, int height, const float color[4]);
void wlr_scene_rect_set_color(struct wlr_scene_rect *rect,
    const float color[4]);

void wlr_scene_rect_set_size(struct wlr_scene_rect *rect, int width, int height);

struct wlr_scene_buffer *wlr_scene_buffer_create(struct wlr_scene_tree *parent,
    struct wlr_buffer *buffer);
void wlr_scene_buffer_set_buffer(struct wlr_scene_buffer *scene_buffer,
    struct wlr_buffer *buffer);
void wlr_scene_buffer_set_buffer_with_damage(
    struct wlr_scene_buffer *scene_buffer,
    struct wlr_buffer *buffer, const pixman_region32_t *region);
void wlr_scene_buffer_set_opaque_region(
    struct wlr_scene_buffer *scene_buffer,
    const pixman_region32_t *region);
void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *scene_buffer,
    const struct wlr_fbox *box);
void wlr_scene_buffer_set_dest_size(struct wlr_scene_buffer *scene_buffer,
    int width, int height);
void wlr_scene_buffer_set_transform(struct wlr_scene_buffer *scene_buffer,
    enum wl_output_transform transform);
void wlr_scene_buffer_set_opacity(struct wlr_scene_buffer *scene_buffer,
    float opacity);
void wlr_scene_buffer_set_filter_mode(struct wlr_scene_buffer *scene_buffer,
    enum wlr_scale_filter_mode filter_mode);
void wlr_scene_buffer_send_frame_done(struct wlr_scene_buffer *scene_buffer,
    const struct timespec *now);

struct wlr_scene_output *wlr_scene_output_create(struct wlr_scene *scene,
    struct wlr_output *output);
void wlr_scene_output_destroy(struct wlr_scene_output *scene_output);
void wlr_scene_output_set_position(struct wlr_scene_output *scene_output,
    int lx, int ly);
bool wlr_scene_output_commit(struct wlr_scene_output *scene_output,
    const struct wlr_scene_output_state_options *options);
bool wlr_scene_output_build_state(struct wlr_scene_output *scene_output,
    struct wlr_output_state *state,
    const struct wlr_scene_output_state_options *options);
void wlr_scene_output_send_frame_done(struct wlr_scene_output *scene_output,
    const struct timespec *now);
void wlr_scene_output_for_each_buffer(struct wlr_scene_output *scene_output,
    wlr_scene_buffer_iterator_func_t iterator, void *user_data);

struct wlr_scene_output_layout *wlr_scene_attach_output_layout(
    struct wlr_scene *scene, struct wlr_output_layout *output_layout);

struct wlr_scene_tree *wlr_scene_subsurface_tree_create(
    struct wlr_scene_tree *parent, struct wlr_surface *surface);
struct wlr_scene_tree *wlr_scene_xdg_surface_create(
    struct wlr_scene_tree *parent, struct wlr_xdg_surface *xdg_surface);

struct wlr_scene_surface *wlr_scene_surface_create(
    struct wlr_scene_tree *parent, struct wlr_surface *surface);

void wlr_scene_set_presentation(struct wlr_scene *scene,
    struct wlr_presentation *presentation);
void wlr_scene_set_linux_dmabuf_v1(struct wlr_scene *scene,
    struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1);

struct wlr_scene_output *wlr_scene_get_scene_output(struct wlr_scene *scene,
    struct wlr_output *output);

struct wlr_scene_tree *wlr_scene_layer_surface_v1_create(
    struct wlr_scene_tree *parent,
    struct wlr_layer_surface_v1 *layer_surface);

struct wlr_scene_tree *wlr_scene_drag_icon_create(
    struct wlr_scene_tree *parent,
    struct wlr_drag_icon *drag_icon);

} /* extern "C" */

#else
/* In C mode, just include the real header */
#include <wlr/types/wlr_scene.h>
#endif

#endif /* ETERNAL_WLR_SCENE_COMPAT_H */
