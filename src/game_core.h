#ifndef GAME_CORE_H
#define GAME_CORE_H

#include <stdint.h>
#include <stdbool.h>

#define GRID_COLS    64
#define GRID_ROWS    64
#define MAX_PLAYERS  8          /* 0 = joueur, 1..7 = bots */
#define MINI_N       32         /* mini-carte : 1 pixel-bloc = 2x2 cases */

enum { DIR_UP = 0, DIR_RIGHT = 1, DIR_DOWN = 2, DIR_LEFT = 3, DIR_NONE = 4 };

/* game_cell_view() : bits 0-3 = id joueur (1..8), 0 = vide */
#define VIEW_ID_MASK 0x0F
#define VIEW_TRAIL   0x10
#define VIEW_HEAD    0x20
#define VIEW_WALL    0x40       /* hors de l'arène circulaire */

/* game_mini_view() : 0 vide, 1..8 propriétaire, MINI_WALL, MINI_YOU */
#define MINI_WALL    9
#define MINI_YOU     10

void    game_seed(uint32_t seed);
void    game_init(void);
void    game_set_direction(int dir);
void    game_tick(void);
bool    game_is_over(void);
bool    game_is_started(void);
void    game_get_scores(int pct[MAX_PLAYERS]);      /* % de l'arène, 0 si mort */
bool    game_player_alive(int p);
void    game_player_pos(int *row, int *col);
uint8_t game_cell_view(int row, int col);           /* accepte toute coordonnée */
uint8_t game_mini_view(int mr, int mc);

#endif
