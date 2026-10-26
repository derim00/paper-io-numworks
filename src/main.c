#include "eadk.h"
#include "game_core.h"

/* Métadonnées lues par l'OS / nwlink */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"), used)) = "Paper.io";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"), used)) = 0;

/* ------------------------------ Disposition ------------------------------ */
#define CELL       12                          /* zone de jeu zoomée */
#define VIEW_COLS  21
#define VIEW_ROWS  18
#define PANEL_X    (VIEW_COLS * CELL)          /* 252 : panneau de droite */
#define BAR_Y      (VIEW_ROWS * CELL)          /* 216 : barre du bas */
#define BAR_H      (EADK_SCREEN_HEIGHT - BAR_Y)
#define MINI_X     (PANEL_X + 2)
#define MINI_Y     4
#define LB_Y       76
#define LB_LINES   5

#define POLL_MS    10
#define TICK_MS    110

/* ------------------------------ Couleurs --------------------------------- */
#define RGB(r, g, b)   ((eadk_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define LIGHT(r, g, b) RGB(((r) + 255) / 2, ((g) + 255) / 2, ((b) + 255) / 2)
#define DARK(r, g, b)  RGB((r) / 2, (g) / 2, (b) / 2)

/* Indirections : forcent le développement de P1..P8 avant RGB/LIGHT/DARK */
#define X_RGB(...)   RGB(__VA_ARGS__)
#define X_LIGHT(...) LIGHT(__VA_ARGS__)
#define X_DARK(...)  DARK(__VA_ARGS__)

#define COL_BG     RGB(232, 234, 238)
#define COL_WALL   RGB(28, 32, 46)
#define COL_HUD    RGB(40, 42, 54)
#define COL_HUD2   RGB(70, 74, 92)
#define COL_MENU   RGB(24, 28, 44)
#define COL_WHITE  RGB(255, 255, 255)
#define COL_GRAY   RGB(120, 124, 140)

/* index 1..8 = joueur puis bots ; 0 = fond */
#define P1 40,110,230
#define P2 220,60,60
#define P3 50,180,80
#define P4 240,160,30
#define P5 160,80,200
#define P6 30,190,190
#define P7 240,110,170
#define P8 190,180,40
static const eadk_color_t c_land[9]  = { COL_BG, X_RGB(P1), X_RGB(P2), X_RGB(P3), X_RGB(P4), X_RGB(P5), X_RGB(P6), X_RGB(P7), X_RGB(P8) };
static const eadk_color_t c_trail[9] = { COL_BG, X_LIGHT(P1), X_LIGHT(P2), X_LIGHT(P3), X_LIGHT(P4), X_LIGHT(P5), X_LIGHT(P6), X_LIGHT(P7), X_LIGHT(P8) };
static const eadk_color_t c_head[9]  = { COL_BG, X_DARK(P1), X_DARK(P2), X_DARK(P3), X_DARK(P4), X_DARK(P5), X_DARK(P6), X_DARK(P7), X_DARK(P8) };

static const char names[MAX_PLAYERS][4] = { "YOU", "BOB", "ZOE", "LEO", "MAX", "ANA", "TIM", "EVA" };

/* ------------------------------ Primitives ------------------------------- */
static void fill(int x, int y, int w, int h, eadk_color_t col) {
  eadk_rect_t r = { (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h };
  eadk_display_push_rect_uniform(r, col);
}

static void text(const char *s, int x, int y, bool large, eadk_color_t fg, eadk_color_t bg) {
  eadk_point_t p = { (uint16_t)x, (uint16_t)y };
  eadk_display_draw_string(s, p, large, fg, bg);
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* police petite ~7 px de large, grande ~10 px */
static void text_center(const char *s, int cx, int y, bool large, eadk_color_t fg, eadk_color_t bg) {
  int w = slen(s) * (large ? 10 : 7);
  text(s, cx - w / 2, y, large, fg, bg);
}

static int put_str(char *b, const char *s) { int n = 0; while (s[n]) { b[n] = s[n]; n++; } return n; }
static int put_int(char *b, int v) {            /* 0..999 */
  int n = 0;
  if (v >= 100) b[n++] = (char)('0' + v / 100);
  if (v >= 10)  b[n++] = (char)('0' + (v / 10) % 10);
  b[n++] = (char)('0' + v % 10);
  return n;
}

static bool key(uint32_t lo, int k) { return (lo >> k) & 1u; }

/* ------------------------------- État ------------------------------------ */
typedef enum { ST_MENU, ST_PLAY, ST_OVER } state_t;

static eadk_color_t shown[VIEW_ROWS][VIEW_COLS];
static uint8_t      mini_shown[MINI_N][MINI_N];
static char         lb_shown[LB_LINES][10];
static uint8_t      lb_id[LB_LINES];
static char         info_shown[28];
static int          last_pct, cam_r, cam_c;
static int          best, peak;

/* ------------------------------ Menu -------------------------------------- */
static void draw_menu(void) {
  fill(0, 0, EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT, COL_MENU);
  /* décor : territoires colorés */
  fill(0, 0, 70, 46, c_land[5]);      fill(0, 0, 38, 24, c_trail[5]);
  fill(250, 0, 70, 56, c_land[4]);    fill(284, 0, 36, 28, c_trail[4]);
  fill(0, 176, 60, 64, c_land[1]);    fill(0, 176, 30, 32, c_trail[1]);
  fill(260, 170, 60, 70, c_land[2]);  fill(290, 205, 30, 35, c_land[3]);
  /* bannière titre */
  fill(60, 52, 200, 56, COL_WHITE);
  fill(64, 56, 192, 48, COL_BG);
  text_center("PAPER.IO", 160, 72, true, COL_WALL, COL_BG);
  /* bouton Jouer */
  fill(100, 126, 120, 38, COL_WHITE);
  fill(103, 129, 114, 32, c_land[3]);
  text_center("JOUER", 160, 138, true, COL_WHITE, c_land[3]);
  if (best > 0) {
    char b[24]; int n = put_str(b, "RECORD "); n += put_int(b + n, best); b[n++] = '%'; b[n] = 0;
    text_center(b, 160, 176, false, COL_WHITE, COL_MENU);
  }
  text_center("OK: jouer    RETOUR: quitter", 160, 216, false, COL_GRAY, COL_MENU);
}

/* ------------------------------ Jeu : rendu ------------------------------- */
static void render_world(bool force) {
  if (game_player_alive(0)) {
    int pr, pc;
    game_player_pos(&pr, &pc);
    cam_r = pr - VIEW_ROWS / 2;
    cam_c = pc - VIEW_COLS / 2;
  }
  for (int vr = 0; vr < VIEW_ROWS; vr++)
    for (int vc = 0; vc < VIEW_COLS; vc++) {
      uint8_t v = game_cell_view(cam_r + vr, cam_c + vc);
      int id = v & VIEW_ID_MASK;
      eadk_color_t col = (v & VIEW_WALL) ? COL_WALL
                       : (v & VIEW_HEAD) ? c_head[id]
                       : (v & VIEW_TRAIL) ? c_trail[id]
                       : c_land[id];
      if (force || shown[vr][vc] != col) {
        fill(vc * CELL, vr * CELL, CELL, CELL, col);
        shown[vr][vc] = col;
      }
    }
}

static void render_minimap(bool force) {
  for (int mr = 0; mr < MINI_N; mr++)
    for (int mc = 0; mc < MINI_N; mc++) {
      uint8_t v = game_mini_view(mr, mc);
      if (force || mini_shown[mr][mc] != v) {
        eadk_color_t col = (v == MINI_WALL) ? COL_WALL : (v == MINI_YOU) ? COL_WHITE : c_land[v];
        fill(MINI_X + mc * 2, MINI_Y + mr * 2, 2, 2, col);
        mini_shown[mr][mc] = v;
      }
    }
}

static void render_leaderboard(bool force, const int pct[MAX_PLAYERS]) {
  int order[MAX_PLAYERS], n = 0;
  for (int p = 0; p < MAX_PLAYERS; p++) {
    if (!game_player_alive(p)) continue;
    int i = n++;
    while (i > 0 && pct[order[i - 1]] < pct[p]) { order[i] = order[i - 1]; i--; }
    order[i] = p;
  }
  for (int k = 0; k < LB_LINES; k++) {
    char line[10];
    int id = 0;
    for (int i = 0; i < 9; i++) line[i] = ' ';
    line[9] = 0;
    if (k < n) {
      int p = order[k], v = pct[p];
      id = p + 1;
      line[0] = (char)('1' + k);
      for (int i = 0; i < 3; i++) line[1 + i] = names[p][i];
      char num[6]; int m = put_int(num, v); num[m++] = '%';   /* aligné à droite sur 4 */
      for (int i = 0; i < m; i++) line[8 - m + i] = num[i];
    }
    bool same = (lb_id[k] == id);
    for (int i = 0; i < 9 && same; i++) if (lb_shown[k][i] != line[i]) same = false;
    if (force || !same) {
      for (int i = 0; i < 10; i++) lb_shown[k][i] = line[i];
      lb_id[k] = (uint8_t)id;
      eadk_color_t bg = (id == 1) ? COL_HUD2 : COL_HUD;
      text(line, PANEL_X + 2, LB_Y + 13 + 12 * k, false, id ? c_land[id] : COL_WHITE, bg);
    }
  }
}

static void render_bar(bool force, const int pct[MAX_PLAYERS]) {
  int me = pct[0];
  if (force || me != last_pct) {
    char b[6]; int n = put_int(b, me); b[n++] = '%';
    while (n < 4) b[n++] = ' ';
    b[n] = 0;
    text(b, 8, BAR_Y + 5, true, COL_WHITE, COL_HUD);
    last_pct = me;
  }
  char info[28];
  for (int i = 0; i < 27; i++) info[i] = ' ';
  info[27] = 0;
  if (!game_is_started()) {
    put_str(info, "FLECHES POUR DEMARRER");
  } else {
    int rank = 1, alive = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
      if (!game_player_alive(p)) continue;
      alive++;
      if (p && pct[p] > me) rank++;
    }
    int n = put_str(info, "RANG "); n += put_int(info + n, rank); info[n++] = '/'; n += put_int(info + n, alive);
    n += put_str(info + n, "  MAX "); n += put_int(info + n, peak); info[n++] = '%';
  }
  bool same = true;
  for (int i = 0; i < 27 && same; i++) if (info_shown[i] != info[i]) same = false;
  if (force || !same) {
    for (int i = 0; i < 28; i++) info_shown[i] = info[i];
    text(info, 70, BAR_Y + 7, false, COL_WHITE, COL_HUD);
  }
}

static void render_hud(bool force) {
  int pct[MAX_PLAYERS];
  game_get_scores(pct);
  if (pct[0] > peak) peak = pct[0];
  if (peak > best) best = peak;
  render_leaderboard(force, pct);
  render_bar(force, pct);
}

static void start_game(void) {
  game_seed(eadk_random());
  game_init();
  peak = 0;
  last_pct = -1;
  for (int i = 0; i < 28; i++) info_shown[i] = 0;
  /* fond des panneaux */
  fill(0, 0, EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT, COL_HUD);
  text("TOP", PANEL_X + 2, LB_Y, false, COL_WHITE, COL_HUD);
  text("BACK:MENU", PANEL_X + 2, BAR_Y - 14, false, COL_GRAY, COL_HUD);
  render_world(true);
  render_minimap(true);
  render_hud(true);
}

/* ------------------------------ Game over --------------------------------- */
#define OV_X 36
#define OV_Y 34
#define OV_W 180
#define OV_H 150

static void draw_over(int sel) {
  int cx = OV_X + OV_W / 2;
  fill(OV_X, OV_Y, OV_W, OV_H, COL_WHITE);
  fill(OV_X + 3, OV_Y + 3, OV_W - 6, OV_H - 6, COL_HUD);
  text_center("GAME OVER", cx, OV_Y + 14, true, c_land[2], COL_HUD);
  char b[28]; int n = put_str(b, "TERRITOIRE MAX "); n += put_int(b + n, peak); b[n++] = '%'; b[n] = 0;
  text_center(b, cx, OV_Y + 44, false, COL_WHITE, COL_HUD);
  const char *label[2] = { "REJOUER", "MENU" };
  for (int i = 0; i < 2; i++) {
    int y = OV_Y + 68 + i * 40;
    eadk_color_t bg = (i == sel) ? c_land[3] : COL_HUD2;
    fill(OV_X + 24, y, OV_W - 48, 32, (i == sel) ? COL_WHITE : COL_HUD2);
    fill(OV_X + 27, y + 3, OV_W - 54, 26, bg);
    text_center(label[i], cx, y + 9, true, COL_WHITE, bg);
  }
}

/* ------------------------------ Boucle ------------------------------------ */
void eadk_main(void) {
  state_t st = ST_MENU;
  uint32_t prev = 0;
  int acc = 0, dir = DIR_NONE, sel = 0;
  best = 0;
  peak = 0;
  cam_r = cam_c = 0;
  draw_menu();

  for (;;) {
    uint32_t lo = (uint32_t)eadk_keyboard_scan();
    uint32_t pressed = lo & ~prev;
    prev = lo;

    if (st == ST_MENU) {
      if (key(pressed, eadk_key_back)) return;               /* quitte l'appli */
      if (key(pressed, eadk_key_ok)) { start_game(); st = ST_PLAY; acc = 0; dir = DIR_NONE; }
    } else if (st == ST_PLAY) {
      if (key(pressed, eadk_key_back)) { st = ST_MENU; draw_menu(); continue; }
      if (key(lo, eadk_key_up))    dir = DIR_UP;
      if (key(lo, eadk_key_down))  dir = DIR_DOWN;
      if (key(lo, eadk_key_left))  dir = DIR_LEFT;
      if (key(lo, eadk_key_right)) dir = DIR_RIGHT;
      acc += POLL_MS;
      if (acc >= TICK_MS) {
        acc = 0;
        game_set_direction(dir);
        dir = DIR_NONE;
        game_tick();
        render_world(false);
        render_minimap(false);
        render_hud(false);
        if (game_is_over()) { st = ST_OVER; sel = 0; draw_over(sel); }
      }
    } else { /* ST_OVER */
      if (key(pressed, eadk_key_up) || key(pressed, eadk_key_down)) { sel ^= 1; draw_over(sel); }
      if (key(pressed, eadk_key_back)) { st = ST_MENU; draw_menu(); }
      else if (key(pressed, eadk_key_ok)) {
        if (sel == 0) { start_game(); st = ST_PLAY; acc = 0; dir = DIR_NONE; }
        else { st = ST_MENU; draw_menu(); }
      }
    }
    eadk_timing_msleep(POLL_MS);
  }
}

/* nwlink 0.0.19 : le _start fourni par le SDK appelle main().
 * Simple relais vers le vrai point d'entrée du jeu. */
int main(void) {
  eadk_main();
  return 0;
}
