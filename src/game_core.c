/* Noyau de jeu Paper.io (port C du coeur JS : grid, player, index).
 * Pur : aucune dépendance à l'EADK ni à la libc.
 * Tout l'état est initialisé explicitement dans game_init() : on ne dépend
 * ni de l'initialisation de .data ni du zéro-fill de .bss par le chargeur. */

#include "game_core.h"

/* ------------------------------------------------------------------ *
 * Fonctions utilitaires libc (non-static : -nostdlib, rien n'est lié)
 * ------------------------------------------------------------------ */
#ifndef HOST_TEST
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dst, int value, unsigned int n) {
  unsigned char *p = (unsigned char *)dst;
  while (n--) *p++ = (unsigned char)value;
  return dst;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *dst, const void *src, unsigned int n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  while (n--) *d++ = *s++;
  return dst;
}
#endif

/* ------------------------------------------------------------------ */

typedef struct {
  int8_t   row, col;
  uint8_t  dir, next_dir;
  bool     alive;
  uint16_t respawn;      /* ticks avant réapparition (bots) */
  uint16_t trail_len;
  uint16_t trail_limit;  /* IA : longueur de traînée avant de rentrer */
  int8_t   home_row, home_col;
} player_t;

static player_t pl[MAX_PLAYERS];
static uint8_t  owner[GRID_ROWS][GRID_COLS];   /* 0 = vide, sinon id+1 */
static uint8_t  trail[GRID_ROWS][GRID_COLS];   /* 0 = aucune, sinon id+1 */
static uint8_t  seen[GRID_ROWS][GRID_COLS];
static uint16_t stk[GRID_ROWS * GRID_COLS];
static int      arena_cells;
static bool     over;
static bool     started;
static uint32_t rng;

static const int8_t DR[4] = { -1, 0, 1, 0 };
static const int8_t DC[4] = { 0, 1, 0, -1 };

/* ----------------------------- RNG ------------------------------- */
static uint32_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
static int rnd_n(int n) { return (int)(rnd() % (uint32_t)n); }

void game_seed(uint32_t seed) { rng = seed ? seed : 0x9E3779B9u; }

/* ----------------------------- Arène ----------------------------- */
/* Arène circulaire : disque inscrit dans la grille. Tout ce qui est en
 * dehors se comporte comme un mur (mort pour un joueur). */
static bool in_arena(int r, int c) {
  if (r < 0 || r >= GRID_ROWS || c < 0 || c >= GRID_COLS) return false;
  int a = 2 * r - (GRID_ROWS - 1);
  int b = 2 * c - (GRID_COLS - 1);
  return a * a + b * b <= (GRID_ROWS - 1) * (GRID_ROWS - 1);
}

static int dir_to_center(int r, int c) {
  int dr = GRID_ROWS / 2 - r, dc = GRID_COLS / 2 - c;
  int adr = dr < 0 ? -dr : dr, adc = dc < 0 ? -dc : dc;
  if (adr >= adc) return dr < 0 ? DIR_UP : DIR_DOWN;
  return dc < 0 ? DIR_LEFT : DIR_RIGHT;
}

/* Équivalent de Player.die() + "Remove dead squares" */
static void kill_player(int p) {
  uint8_t id = (uint8_t)(p + 1);
  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++) {
      if (owner[r][c] == id) owner[r][c] = 0;
      if (trail[r][c] == id) trail[r][c] = 0;
    }
  pl[p].alive = false;
  pl[p].trail_len = 0;
  pl[p].respawn = (uint16_t)(30 + rnd_n(40));
  if (p == 0) over = true;
}

/* Équivalent de Tail.fillTail() : la traînée devient territoire, puis tout
 * ce qui n'est pas atteignable depuis l'extérieur de l'arène sans traverser
 * le territoire du joueur est enclos => capturé. */
static void commit_trail_and_fill(int p) {
  uint8_t id = (uint8_t)(p + 1);
  int sp = 0;

  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++) {
      if (trail[r][c] == id) { owner[r][c] = id; trail[r][c] = 0; }
      seen[r][c] = 0;
    }

  /* Semis : cases de l'arène au contact du vide extérieur, pas à nous */
  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++) {
      if (!in_arena(r, c) || owner[r][c] == id) continue;
      bool edge = false;
      for (int d = 0; d < 4; d++)
        if (!in_arena(r + DR[d], c + DC[d])) { edge = true; break; }
      if (edge) {
        seen[r][c] = 1;
        stk[sp++] = (uint16_t)(r * GRID_COLS + c);
      }
    }

  while (sp > 0) {
    int idx = stk[--sp];
    int r = idx / GRID_COLS, c = idx % GRID_COLS;
    for (int d = 0; d < 4; d++) {
      int nr = r + DR[d], nc = c + DC[d];
      if (in_arena(nr, nc) && !seen[nr][nc] && owner[nr][nc] != id) {
        seen[nr][nc] = 1;
        stk[sp++] = (uint16_t)(nr * GRID_COLS + nc);
      }
    }
  }

  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++)
      if (in_arena(r, c) && !seen[r][c]) owner[r][c] = id;
}

/* Équivalent de initPlayer() : territoire de départ 3x3 */
static bool spawn_player(int p) {
  for (int tries = 0; tries < 200; tries++) {
    int r = 3 + rnd_n(GRID_ROWS - 6);
    int c = 3 + rnd_n(GRID_COLS - 6);
    bool free_zone = true;
    for (int dr = -2; dr <= 2 && free_zone; dr++)
      for (int dc = -2; dc <= 2; dc++)
        if (!in_arena(r + dr, c + dc) || owner[r + dr][c + dc] || trail[r + dr][c + dc]) {
          free_zone = false;
          break;
        }
    if (!free_zone) continue;

    for (int dr = -1; dr <= 1; dr++)
      for (int dc = -1; dc <= 1; dc++)
        owner[r + dr][c + dc] = (uint8_t)(p + 1);

    pl[p].row = (int8_t)r;  pl[p].col = (int8_t)c;
    pl[p].home_row = (int8_t)r;  pl[p].home_col = (int8_t)c;
    pl[p].dir = pl[p].next_dir = (uint8_t)dir_to_center(r, c);
    pl[p].alive = true;
    pl[p].trail_len = 0;
    pl[p].trail_limit = (uint16_t)(6 + rnd_n(16));
    return true;
  }
  return false;
}

void game_init(void) {
  arena_cells = 0;
  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++) {
      owner[r][c] = 0; trail[r][c] = 0; seen[r][c] = 0;
      if (in_arena(r, c)) arena_cells++;
    }
  for (int p = 0; p < MAX_PLAYERS; p++) {
    pl[p].alive = false;
    pl[p].respawn = (uint16_t)(p * 6);
    pl[p].trail_len = 0;
    pl[p].row = pl[p].col = 0;
  }
  over = false;
  started = false;
  if (rng == 0) game_seed(1);
  spawn_player(0);
  for (int p = 1; p < MAX_PLAYERS; p++) spawn_player(p);
}

void game_set_direction(int dir) {
  if (dir < 0 || dir > 3 || over) return;
  started = true;
  if (dir != ((pl[0].dir + 2) & 3)) pl[0].next_dir = (uint8_t)dir;
}

/* ------------------------------ IA ------------------------------- */
static bool bot_cell_ok(int p, int r, int c) {
  return in_arena(r, c) && trail[r][c] != (uint8_t)(p + 1);
}

static void bot_think(int p) {
  player_t *b = &pl[p];
  int want = b->dir;
  bool outside = owner[b->row][b->col] != (uint8_t)(p + 1);

  if (outside && b->trail_len >= b->trail_limit) {
    int dr = b->home_row - b->row, dc = b->home_col - b->col;
    int adr = dr < 0 ? -dr : dr, adc = dc < 0 ? -dc : dc;
    if (adr >= adc) want = (dr < 0) ? DIR_UP : DIR_DOWN;
    else            want = (dc < 0) ? DIR_LEFT : DIR_RIGHT;
  } else if (rnd_n(7) == 0) {
    want = (b->dir + (rnd_n(2) ? 1 : 3)) & 3;
  }

  const int order[4] = { want, (want + 1) & 3, (want + 3) & 3, (want + 2) & 3 };
  for (int i = 0; i < 4; i++) {
    int d = order[i];
    if (d == ((b->dir + 2) & 3)) continue;           /* pas de demi-tour */
    if (bot_cell_ok(p, b->row + DR[d], b->col + DC[d])) {
      b->next_dir = (uint8_t)d;
      return;
    }
  }
}

/* ------------------------------ Tick ----------------------------- */
void game_tick(void) {
  int8_t old_r[MAX_PLAYERS], old_c[MAX_PLAYERS];
  int8_t new_r[MAX_PLAYERS], new_c[MAX_PLAYERS];
  bool   dying[MAX_PLAYERS];

  if (over || !started) return;

  for (int p = 1; p < MAX_PLAYERS; p++) {
    if (!pl[p].alive) {
      if (pl[p].respawn > 0) pl[p].respawn--;
      else spawn_player(p);
    } else {
      bot_think(p);
    }
  }

  /* Phase 1 : positions cibles */
  for (int p = 0; p < MAX_PLAYERS; p++) {
    dying[p] = false;
    old_r[p] = pl[p].row;  old_c[p] = pl[p].col;
    new_r[p] = old_r[p];   new_c[p] = old_c[p];
    if (!pl[p].alive) continue;
    pl[p].dir = pl[p].next_dir;
    new_r[p] = (int8_t)(old_r[p] + DR[pl[p].dir]);
    new_c[p] = (int8_t)(old_c[p] + DC[pl[p].dir]);
    if (!in_arena(new_r[p], new_c[p])) dying[p] = true;    /* hors de l'arène */
  }

  /* Phase 2 : collisions tête contre tête */
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!pl[i].alive || dying[i]) continue;
    for (int j = i + 1; j < MAX_PLAYERS; j++) {
      if (!pl[j].alive || dying[j]) continue;
      bool same = (new_r[i] == new_r[j] && new_c[i] == new_c[j]);
      bool swap = (new_r[i] == old_r[j] && new_c[i] == old_c[j] &&
                   new_r[j] == old_r[i] && new_c[j] == old_c[i]);
      if (same) {
        uint8_t o = owner[new_r[i]][new_c[i]];
        if (o == (uint8_t)(i + 1))      dying[j] = true;  /* l'occupant du territoire gagne */
        else if (o == (uint8_t)(j + 1)) dying[i] = true;
        else { dying[i] = true; dying[j] = true; }
      } else if (swap) {
        dying[i] = true; dying[j] = true;
      }
    }
  }
  for (int p = 0; p < MAX_PLAYERS; p++)
    if (dying[p] && pl[p].alive) kill_player(p);

  /* Phase 3 : déplacement, traînées, capture */
  for (int p = 0; p < MAX_PLAYERS; p++) {
    if (!pl[p].alive) continue;     /* peut avoir été tué par la traînée d'un autre */
    int r = new_r[p], c = new_c[p];
    uint8_t id = (uint8_t)(p + 1);

    pl[p].row = (int8_t)r;  pl[p].col = (int8_t)c;

    uint8_t t = trail[r][c];
    if (t) {                        /* Tail.hitsTail() */
      if (t == id) { kill_player(p); continue; }
      kill_player(t - 1);           /* le propriétaire de la traînée meurt */
    }

    if (owner[r][c] == id) {
      if (pl[p].trail_len > 0) {
        commit_trail_and_fill(p);
        pl[p].trail_len = 0;
        pl[p].trail_limit = (uint16_t)(6 + rnd_n(16));
      }
    } else {
      trail[r][c] = id;
      pl[p].trail_len++;
    }
  }
}

/* ---------------------------- Lecture ---------------------------- */
bool game_is_over(void)    { return over; }
bool game_is_started(void) { return started; }
bool game_player_alive(int p) { return p >= 0 && p < MAX_PLAYERS && pl[p].alive; }

void game_player_pos(int *row, int *col) { *row = pl[0].row; *col = pl[0].col; }

void game_get_scores(int pct[MAX_PLAYERS]) {
  int cnt[MAX_PLAYERS];
  for (int p = 0; p < MAX_PLAYERS; p++) cnt[p] = 0;
  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++) {
      uint8_t o = owner[r][c];
      if (o) cnt[o - 1]++;
    }
  for (int p = 0; p < MAX_PLAYERS; p++)
    pct[p] = (arena_cells > 0) ? (cnt[p] * 100) / arena_cells : 0;
}

uint8_t game_cell_view(int row, int col) {
  if (!in_arena(row, col)) return VIEW_WALL;
  uint8_t v = owner[row][col] & VIEW_ID_MASK;
  if (trail[row][col]) v = (uint8_t)((trail[row][col] & VIEW_ID_MASK) | VIEW_TRAIL);
  for (int p = 0; p < MAX_PLAYERS; p++)
    if (pl[p].alive && pl[p].row == row && pl[p].col == col)
      return (uint8_t)((p + 1) | VIEW_HEAD);
  return v;
}

uint8_t game_mini_view(int mr, int mc) {
  bool any = false;
  uint8_t own = 0;
  for (int dr = 0; dr < 2; dr++)
    for (int dc = 0; dc < 2; dc++) {
      int r = mr * 2 + dr, c = mc * 2 + dc;
      if (!in_arena(r, c)) continue;
      any = true;
      if (pl[0].alive && pl[0].row == r && pl[0].col == c) return MINI_YOU;
      if (!own && owner[r][c]) own = owner[r][c];
    }
  return any ? own : MINI_WALL;
}
