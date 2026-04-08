#include <pebble.h>

// ── Pose IDs ─────────────────────────────────────────────────────────────────
typedef enum {
  POSE_IDLE = 0,
  POSE_ARMS_UP,
  POSE_LEAN_LEFT,
  POSE_LEAN_RIGHT,
  POSE_JUMP,
  POSE_BOW,
  POSE_SPIN,
  NUM_POSES
} DinoPos;

static const uint32_t POSE_RESOURCES[NUM_POSES] = {
  RESOURCE_ID_DINO_IDLE,
  RESOURCE_ID_DINO_ARMS_UP,
  RESOURCE_ID_DINO_LEAN_LEFT,
  RESOURCE_ID_DINO_LEAN_RIGHT,
  RESOURCE_ID_DINO_JUMP,
  RESOURCE_ID_DINO_BOW,
  RESOURCE_ID_DINO_SPIN,
};

static const char *POSE_LABELS[NUM_POSES] = {
  "Ready!",
  "Woohoo!",
  "Lean Left!",
  "Lean Right!",
  "Jump!",
  "Ta-da!",
  "Spin!",
};

// ── State ─────────────────────────────────────────────────────────────────────
static Window     *s_window;
static Layer      *s_canvas_layer;
static GBitmap    *s_pose_bitmap    = NULL;
static BitmapLayer *s_bitmap_layer  = NULL;
static DinoPos     s_pose           = POSE_IDLE;
static AppTimer   *s_anim_timer     = NULL;
static int         s_frame          = 0;
// Brief flash effect when a pose changes
static bool        s_flash          = false;
static int         s_flash_frames   = 0;

// ── Load bitmap for current pose ──────────────────────────────────────────────
static void load_pose_bitmap(void) {
  if (s_pose_bitmap) {
    gbitmap_destroy(s_pose_bitmap);
    s_pose_bitmap = NULL;
  }
  s_pose_bitmap = gbitmap_create_with_resource(POSE_RESOURCES[s_pose]);
  bitmap_layer_set_bitmap(s_bitmap_layer, s_pose_bitmap);
}

// ── Canvas overlay (title + label) ────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Flash highlight on pose change
  if (s_flash) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    return;  // skip drawing this frame for snap effect
  }

  // Title bar
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, 20), 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx,
    "Dancing Dino",
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(0, 2, bounds.size.w, 18),
    GTextOverflowModeWordWrap,
    GTextAlignmentCenter,
    NULL);

  // Pose label at bottom
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, bounds.size.h - 24, bounds.size.w, 24),
                     0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx,
    POSE_LABELS[s_pose],
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(0, bounds.size.h - 23, bounds.size.w, 22),
    GTextOverflowModeWordWrap,
    GTextAlignmentCenter,
    NULL);

  // Button hint (tiny)
  /*graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx,
    "UP \xe2\x80\xa2 MID \xe2\x80\xa2 DN",   // "UP · MID · DN" in UTF-8
    fonts_get_system_font(FONT_KEY_GOTHIC_09),
    GRect(0, bounds.size.h - 36, bounds.size.w, 12),
    GTextOverflowModeWordWrap,
    GTextAlignmentCenter,
    NULL);*/
}

// ── Animation tick ─────────────────────────────────────────────────────────────
static void anim_callback(void *ctx) {
  s_frame++;

  if (s_flash) {
    s_flash_frames--;
    if (s_flash_frames <= 0) s_flash = false;
  }

  layer_mark_dirty(s_canvas_layer);
  s_anim_timer = app_timer_register(80, anim_callback, NULL);
}

// ── Pose change helper ────────────────────────────────────────────────────────
static void set_pose(DinoPos new_pose) {
  if (new_pose == s_pose) return;
  s_pose = new_pose;
  load_pose_bitmap();
  s_flash        = true;
  s_flash_frames = 1;        // 1-frame white flash
  layer_mark_dirty(s_canvas_layer);
}

// ── Button handlers ───────────────────────────────────────────────────────────
static void up_click(ClickRecognizerRef r, void *ctx) {
  // UP cycles: Idle → Arms Up → Jump → Spin → back to Arms Up
  switch (s_pose) {
    case POSE_IDLE:       set_pose(POSE_ARMS_UP);   break;
    case POSE_ARMS_UP:    set_pose(POSE_JUMP);       break;
    case POSE_JUMP:       set_pose(POSE_SPIN);       break;
    default:              set_pose(POSE_ARMS_UP);    break;
  }
}

static void select_click(ClickRecognizerRef r, void *ctx) {
  // SELECT: Bow (ta-da / curtain call) — or return to idle if already bowing
  if (s_pose == POSE_BOW) {
    set_pose(POSE_IDLE);
  } else {
    set_pose(POSE_BOW);
  }
}

static void down_click(ClickRecognizerRef r, void *ctx) {
  // DOWN cycles: Idle → Lean Left → Lean Right → back to Lean Left
  switch (s_pose) {
    case POSE_IDLE:         set_pose(POSE_LEAN_LEFT);  break;
    case POSE_LEAN_LEFT:    set_pose(POSE_LEAN_RIGHT); break;
    case POSE_LEAN_RIGHT:   set_pose(POSE_LEAN_LEFT);  break;
    default:                set_pose(POSE_LEAN_LEFT);  break;
  }
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click);
}

// ── Window lifecycle ──────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  // Bitmap layer — centred in the remaining space between title bar and label
  // Title bar: 20 px top. Label: 24 px bottom. Hints: 12 px above label.
  // Load initial pose first so we can read its size
  load_pose_bitmap();
  
  // Get raw image size
  GRect bmp_bounds = gbitmap_get_bounds(s_pose_bitmap);
  int bmp_w = bmp_bounds.size.w;
  int bmp_h = bmp_bounds.size.h;
  
  // Available space (same logic you already had)
  int content_top    = 20;
  int content_bottom = bounds.size.h - 24 - 12;
  int content_height = content_bottom - content_top;
  int content_width  = bounds.size.w;
  
  // Compute scale factor (integer math, Pebble-style)
  int scale_w = content_width / bmp_w;
  int scale_h = content_height / bmp_h;
  int scale   = scale_w < scale_h ? scale_w : scale_h;
  
  // Prevent zero or silly scaling
  if (scale < 1) scale = 1;
  
  // Final size
  int img_w = bmp_w * scale;
  int img_h = bmp_h * scale;
  
  // Center it
  int img_x = (bounds.size.w - img_w) / 2;
  int img_y = content_top + (content_height - img_h) / 2;
  
  // Create layer with scaled size
  s_bitmap_layer = bitmap_layer_create(GRect(img_x, img_y, img_w, img_h));
  bitmap_layer_set_compositing_mode(s_bitmap_layer, GCompOpSet);
  bitmap_layer_set_background_color(s_bitmap_layer, GColorWhite);
  
  layer_add_child(root, bitmap_layer_get_layer(s_bitmap_layer));
  bitmap_layer_set_bitmap(s_bitmap_layer, s_pose_bitmap);
  bitmap_layer_set_compositing_mode(s_bitmap_layer, GCompOpSet);
  bitmap_layer_set_background_color(s_bitmap_layer, GColorWhite);
  layer_add_child(root, bitmap_layer_get_layer(s_bitmap_layer));

  // Overlay canvas (title, label, flash)
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  // Load initial pose
  load_pose_bitmap();

  // Start animation timer
  s_anim_timer = app_timer_register(80, anim_callback, NULL);
}

static void window_unload(Window *window) {
  if (s_anim_timer) { app_timer_cancel(s_anim_timer); s_anim_timer = NULL; }
  layer_destroy(s_canvas_layer);
  bitmap_layer_destroy(s_bitmap_layer);
  if (s_pose_bitmap) { gbitmap_destroy(s_pose_bitmap); s_pose_bitmap = NULL; }
}

// ── App init/deinit ───────────────────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}