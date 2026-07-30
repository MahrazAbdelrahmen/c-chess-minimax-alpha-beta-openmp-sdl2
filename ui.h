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
#define BOARD_OFFSET_X 32
#define BOARD_OFFSET_Y 60
#define PANEL_WIDTH 384
#define PANEL_X (BOARD_OFFSET_X + TILE_SIZE * BOARD_SIZE + 32)

#define BOARD_LIGHT_R 240
#define BOARD_LIGHT_G 217
#define BOARD_LIGHT_B 181
#define BOARD_DARK_R 181
#define BOARD_DARK_G 136
#define BOARD_DARK_B 99

typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_GAME_OVER,
    STATE_PROMOTION
} GameState;

typedef enum {
    MENU_PVPC,       // pc vs pc
    MENU_HVPC_BLACK, // human (black) vs pc (white)
    MENU_HVPC_WHITE, // human (white) vs pc (black)
    MENU_QUIT
} MenuOption;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font_large;
    TTF_Font* font_medium;
    TTF_Font* font_small;
    
    // we index white_pieces/black_pieces as k, q, b, n, r, p
    SDL_Texture* white_pieces[6];
    SDL_Texture* black_pieces[6];
    SDL_Texture* board_texture;
    SDL_Texture* bg_texture;

    GameState state;
    MenuOption selected_option;

    int selected_x, selected_y;
    int last_move_sx, last_move_sy, last_move_dx, last_move_dy;
    int valid_moves[8][8];
    int num_valid_moves;

    int promotion_row, promotion_col;
    char promotion_choice;

    // we encode game_mode as 1=pc vs pc, 2=human vs pc (black), 3=human vs pc (white)
    int game_mode;
    int est_white, est_black;
    int depth, width;
    int omp_threads, omp_procs;

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

void interface_dessiner_plateau(UIContext* ctx, struct config* conf);
void interface_dessiner_pieces(UIContext* ctx, struct config* conf);
void interface_dessiner_panneau(UIContext* ctx, struct config* conf, int num_coup, char* dernier_coup, int player);
void interface_dessiner_menu(UIContext* ctx);
void interface_dessiner_fin_partie(UIContext* ctx, int winner);
void interface_dessiner_promotion(UIContext* ctx, int is_white);

void interface_dessiner_piece(UIContext* ctx, int piece, int x, int y, int alpha);
void interface_dessiner_case(UIContext* ctx, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void interface_dessiner_rectangle(UIContext* ctx, int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void interface_dessiner_texte(UIContext* ctx, const char* text, int x, int y, int size, Uint8 r, Uint8 g, Uint8 b);

#endif
