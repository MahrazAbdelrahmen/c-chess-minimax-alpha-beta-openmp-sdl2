#ifndef UI_H
#define UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include "jeu.h"

#define SCREEN_WIDTH 1120
#define SCREEN_HEIGHT 760
#define BOARD_SIZE 8
#define TILE_SIZE 80
#define BOARD_OFFSET_X 40
#define BOARD_OFFSET_Y 60
#define PANEL_WIDTH 368
#define PANEL_X (BOARD_OFFSET_X + TILE_SIZE * BOARD_SIZE + 40)

#define PAL_BACKGROUND 0x19151E
#define PAL_SURFACE    0x211C28
#define PAL_BORDER     0x3B3444
#define PAL_FOREGROUND 0xF0EDF2
#define PAL_MUTED      0x9A90A6
#define PAL_ACCENT     0xE3EF8B
#define PAL_SERIES_2   0xBFA4EE
#define PAL_SERIES_3   0x91DAC9
#define PAL_WARM       0xF69270
#define PAL_HEAT_0     0x292131
#define PAL_HEAT_1     0x514064
#define PAL_HEAT_2     0x976787
#define PAL_HEAT_3     0xD89A91
#define PAL_HEAT_4     0xE3EF8B
#define PAL_NEGATIVE   PAL_SERIES_3
#define PAL_POSITIVE   PAL_WARM

#define PAL_R(hex) ((Uint8)(((hex) >> 16) & 0xFF))
#define PAL_G(hex) ((Uint8)(((hex) >> 8) & 0xFF))
#define PAL_B(hex) ((Uint8)((hex) & 0xFF))

#define BOARD_LIGHT PAL_HEAT_3
#define BOARD_DARK  PAL_HEAT_2

#define FONT_TITLE  56
#define FONT_LARGE  28
#define FONT_MEDIUM 20
#define FONT_SMALL  14
#define FONT_TINY   12

typedef enum {
    MENU_ROW_WHITE_EVAL,
    MENU_ROW_WHITE_SEARCH,
    MENU_ROW_BLACK_EVAL,
    MENU_ROW_BLACK_SEARCH,
    MENU_ROW_DEPTH,
    MENU_ROW_WIDTH,
    MENU_ROW_BENCHMARK,
    MENU_ROW_COUNT
} MenuRow;

typedef enum {
    MENU_CTRL_DECREMENT,
    MENU_CTRL_INCREMENT,
    MENU_CTRL_ALL
} MenuControl;

typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_GAME_OVER,
    STATE_PROMOTION
} GameState;

typedef enum {
    MENU_PVPC,  // pc vs pc
    MENU_HVPC_BLACK,  // human (black) vs pc (white)
    MENU_HVPC_WHITE,  // human (white) vs pc (black)
    MENU_QUIT
} MenuOption;

typedef struct {
    int valid;
    TypeRecherche algo;
    double seconds;
    long long nodes;
    int threads;
} DerniereRecherche;

typedef struct {
    int compared;
    int same_move;
    int same_score;
    double seq_seconds, algo_seconds;
    long long seq_nodes, algo_nodes;
} StatsBenchmark;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font_title;
    TTF_Font* font_large;
    TTF_Font* font_medium;
    TTF_Font* font_small;
    TTF_Font* font_tiny;

    // we index white_pieces/black_pieces as k, q, b, n, r, p
    SDL_Texture* white_pieces[6];
    SDL_Texture* black_pieces[6];

    GameState state;
    MenuOption selected_option;

    int selected_x, selected_y;
    int last_move_sx, last_move_sy, last_move_dx, last_move_dy;
    int valid_moves[8][8];
    int num_valid_moves;

    int promotion_row, promotion_col;
    char promotion_choice;

    int game_mode;
    int est_white, est_black;
    int depth, width;
    int omp_threads, omp_procs;

    TypeRecherche algo_white, algo_black;
    int benchmark;
    int thinking;
    double thinking_seconds;
    DerniereRecherche last_search[2];
    StatsBenchmark bench[RECHERCHE_COUNT];

    float drag_x, drag_y;
    int dragging;
    int drag_piece;
    int drag_start_x, drag_start_y;

    char status_msg[100];
    int show_message;
} UIContext;

int interface_initialiser(UIContext* ctx);
void interface_nettoyer(UIContext* ctx);
void interface_charger_pieces(UIContext* ctx);

void interface_effacer_ecran(UIContext* ctx);
void interface_dessiner_plateau(UIContext* ctx, struct config* conf);
void interface_dessiner_pieces(UIContext* ctx, struct config* conf);
void interface_dessiner_panneau(UIContext* ctx, struct config* conf, int num_coup, char* dernier_coup, int player);
void interface_dessiner_menu(UIContext* ctx);
void interface_dessiner_fin_partie(UIContext* ctx, int winner);
void interface_dessiner_promotion(UIContext* ctx, int is_white);
int interface_option_promotion(int mx, int my);
int interface_menu_carte(int mx, int my);
int interface_menu_controle(int mx, int my, int* row, int* control);
const char* interface_nom_algo(TypeRecherche algo);

void interface_dessiner_piece(UIContext* ctx, int piece, int x, int y, int alpha);
void interface_dessiner_case(UIContext* ctx, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void interface_dessiner_rectangle(UIContext* ctx, int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void interface_dessiner_texte(UIContext* ctx, const char* text, int x, int y, int size, Uint8 r, Uint8 g, Uint8 b);

#endif
