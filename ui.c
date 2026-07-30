#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* mode_labels[] = {
    "PC vs PC",
    "Human as Black",
    "Human as White"
};

static const char* heuristic_labels[] = {
    "1 Material",
    "2 Center+King",
    "3 Material+Noise",
    "4 Threats",
    "5 Center",
    "6 Combined",
    "7 Random"
};

static void interface_dessiner_texte_centre(UIContext* ctx, const char* text, int center_x, int y, int size,
    Uint8 r, Uint8 g, Uint8 b) {
    TTF_Font* font = ctx->font_medium;
    if (size == 28) font = ctx->font_large;
    else if (size == 14) font = ctx->font_small;

    if (!font) return;

    int text_w = 0;
    int text_h = 0;
    if (TTF_SizeText(font, text, &text_w, &text_h) != 0) return;
    interface_dessiner_texte(ctx, text, center_x - text_w / 2, y, size, r, g, b);
}

static int point_dans_rectangle(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void interface_dessiner_controle_menu(UIContext* ctx, int x, int y, int w, int h, const char* label, int hovered) {
    Uint8 bg_r = hovered ? 90 : 64;
    Uint8 bg_g = hovered ? 102 : 72;
    Uint8 bg_b = hovered ? 118 : 84;
    Uint8 accent_r = hovered ? 250 : 232;
    Uint8 accent_g = hovered ? 214 : 198;
    Uint8 accent_b = hovered ? 130 : 108;

    interface_dessiner_rectangle(ctx, x, y, w, h, bg_r, bg_g, bg_b, 255);
    interface_dessiner_rectangle(ctx, x, y, 4, h, accent_r, accent_g, accent_b, 255);
    interface_dessiner_texte_centre(ctx, label, x + w / 2, y + 6, 20, 255, 255, 255);
}

static void interface_dessiner_carte_mode(UIContext* ctx, int x, int y, int w, int h, const char* title,
    const char* subtitle, int selected, int hovered) {
    Uint8 bg_r = selected ? 82 : (hovered ? 52 : 35);
    Uint8 bg_g = selected ? 118 : (hovered ? 63 : 40);
    Uint8 bg_b = selected ? 86 : (hovered ? 78 : 50);
    Uint8 stripe_r = selected ? 232 : (hovered ? 196 : 108);
    Uint8 stripe_g = selected ? 198 : (hovered ? 176 : 96);
    Uint8 stripe_b = selected ? 108 : (hovered ? 122 : 60);

    interface_dessiner_rectangle(ctx, x, y, w, h, bg_r, bg_g, bg_b, 255);
    interface_dessiner_rectangle(ctx, x, y, 10, h, stripe_r, stripe_g, stripe_b, 255);
    interface_dessiner_rectangle(ctx, x + 18, y + h - 10, w - 36, 2, 116, 118, 126, hovered ? 220 : 140);
    interface_dessiner_texte(ctx, title, x + 28, y + 14, 20, 255, 255, 255);
    interface_dessiner_texte(ctx, subtitle, x + 28, y + 42, 14, 190, 198, 206);
}

static const char* libelle_mode(int game_mode) {
    if (game_mode >= 1 && game_mode <= 3) {
        return mode_labels[game_mode - 1];
    }
    return "Unknown";
}

static const char* libelle_heuristique(int heuristic) {
    if (heuristic >= 0 && heuristic < (int)(sizeof(heuristic_labels) / sizeof(heuristic_labels[0]))) {
        return heuristic_labels[heuristic];
    }
    return "Unknown";
}

static void compter_pieces(struct config* conf, int* white_pieces, int* black_pieces) {
    *white_pieces = 0;
    *black_pieces = 0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (conf->mat[y][x] > 0) (*white_pieces)++;
            else if (conf->mat[y][x] < 0) (*black_pieces)++;
        }
    }
}

int interface_initialiser(UIContext* ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 0;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("SDL_image could not initialize! IMG_Error: %s\n", IMG_GetError());
        return 0;
    }

    if (TTF_Init() < 0) {
        printf("SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError());
        return 0;
    }

    ctx->window = SDL_CreateWindow("Chess Engine - Alpha Beta",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    
    if (!ctx->window) {
        printf("Window could not be created! SDL Error: %s\n", SDL_GetError());
        return 0;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    if (!ctx->renderer) {
        printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        return 0;
    }

    // we try these system font paths in order and use whichever loads first
    const char* font_paths[] = {
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "arial.ttf",
        "DejaVuSans.ttf"
    };
    
    ctx->font_large = NULL;
    ctx->font_medium = NULL;
    ctx->font_small = NULL;
    
    for (int i = 0; i < 5; i++) {
        if (!ctx->font_large) ctx->font_large = TTF_OpenFont(font_paths[i], 28);
        if (!ctx->font_medium) ctx->font_medium = TTF_OpenFont(font_paths[i], 20);
        if (!ctx->font_small) ctx->font_small = TTF_OpenFont(font_paths[i], 14);
    }

    ctx->state = STATE_MENU;
    ctx->selected_option = MENU_PVPC;
    ctx->selected_x = -1;
    ctx->selected_y = -1;
    ctx->last_move_sx = -1;
    ctx->last_move_sy = -1;
    ctx->last_move_dx = -1;
    ctx->last_move_dy = -1;
    ctx->dragging = 0;
    ctx->drag_piece = 0;
    ctx->game_mode = 1;
    ctx->est_white = 6;
    ctx->est_black = 6;
    ctx->depth = 5;
    ctx->width = 0;
    ctx->show_message = 0;
    
    for (int i = 0; i < 6; i++) {
        ctx->white_pieces[i] = NULL;
        ctx->black_pieces[i] = NULL;
    }
    ctx->board_texture = NULL;
    ctx->bg_texture = NULL;
    
    memset(ctx->valid_moves, 0, sizeof(ctx->valid_moves));
    
    return 1;
}

void interface_nettoyer(UIContext* ctx) {
    for (int i = 0; i < 6; i++) {
        if (ctx->white_pieces[i]) SDL_DestroyTexture(ctx->white_pieces[i]);
        if (ctx->black_pieces[i]) SDL_DestroyTexture(ctx->black_pieces[i]);
    }
    if (ctx->board_texture) SDL_DestroyTexture(ctx->board_texture);
    if (ctx->bg_texture) SDL_DestroyTexture(ctx->bg_texture);
    
    if (ctx->font_large) TTF_CloseFont(ctx->font_large);
    if (ctx->font_medium) TTF_CloseFont(ctx->font_medium);
    if (ctx->font_small) TTF_CloseFont(ctx->font_small);
    if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window) SDL_DestroyWindow(ctx->window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void interface_charger_pieces(UIContext* ctx) {
    // we index these by the same r/n/f/c/t/p codes jeu.c uses for pieces
    const char* white_piece_files[] = {
        "chess_green/white_king.png",
        "chess_green/white_queen.png",
        "chess_green/white_bishop.png",
        "chess_green/white_knight.png",
        "chess_green/white_rook.png",
        "chess_green/white_pawn.png"
    };

    const char* black_piece_files[] = {
        "chess_green/black_king.png",
        "chess_green/black_queen.png",
        "chess_green/black_bishop.png",
        "chess_green/black_knight.png",
        "chess_green/black_rook.png",
        "chess_green/black_pawn.png"
    };

    printf("Loading piece images...\n");

    for (int i = 0; i < 6; i++) {
        ctx->white_pieces[i] = NULL;
        SDL_Surface* surface = IMG_Load(white_piece_files[i]);
        if (surface) {
            ctx->white_pieces[i] = SDL_CreateTextureFromSurface(ctx->renderer, surface);
            SDL_FreeSurface(surface);
            if (ctx->white_pieces[i]) {
                printf("  Loaded: %s\n", white_piece_files[i]);
            } else {
                printf("  Failed to create texture: %s\n", white_piece_files[i]);
            }
        } else {
            printf("  Failed to load: %s - %s\n", white_piece_files[i], IMG_GetError());
        }
    }
    
    for (int i = 0; i < 6; i++) {
        ctx->black_pieces[i] = NULL;
        SDL_Surface* surface = IMG_Load(black_piece_files[i]);
        if (surface) {
            ctx->black_pieces[i] = SDL_CreateTextureFromSurface(ctx->renderer, surface);
            SDL_FreeSurface(surface);
            if (ctx->black_pieces[i]) {
                printf("  Loaded: %s\n", black_piece_files[i]);
            } else {
                printf("  Failed to create texture: %s\n", black_piece_files[i]);
            }
        } else {
            printf("  Failed to load: %s - %s\n", black_piece_files[i], IMG_GetError());
        }
    }
    
    ctx->board_texture = NULL;
    SDL_Surface* board_surf = IMG_Load("chess_green/board.png");
    if (board_surf) {
        ctx->board_texture = SDL_CreateTextureFromSurface(ctx->renderer, board_surf);
        SDL_FreeSurface(board_surf);
        if (ctx->board_texture) {
            printf("  Loaded board texture\n");
        }
    } else {
        printf("  No board texture found, using fallback\n");
    }
    
    ctx->bg_texture = NULL;
    SDL_Surface* bg_surf = IMG_Load("chess_green/bg.png");
    if (bg_surf) {
        ctx->bg_texture = SDL_CreateTextureFromSurface(ctx->renderer, bg_surf);
        SDL_FreeSurface(bg_surf);
        if (ctx->bg_texture) {
            printf("  Loaded background texture\n");
        }
    }
}

void interface_dessiner_rectangle(UIContext* ctx, int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, r, g, b, a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(ctx->renderer, &rect);
}

void interface_dessiner_case(UIContext* ctx, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    int px = BOARD_OFFSET_X + x * TILE_SIZE;
    int py = BOARD_OFFSET_Y + y * TILE_SIZE;
    interface_dessiner_rectangle(ctx, px, py, TILE_SIZE, TILE_SIZE, r, g, b, a);
}

void interface_dessiner_texte(UIContext* ctx, const char* text, int x, int y, int size, Uint8 r, Uint8 g, Uint8 b) {
    TTF_Font* font = ctx->font_medium;
    if (size == 28) font = ctx->font_large;
    else if (size == 14) font = ctx->font_small;
    
    if (!font) return;
    
    SDL_Color color = {r, g, b, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
        SDL_Rect rect = {x, y, surface->w, surface->h};
        SDL_RenderCopy(ctx->renderer, texture, NULL, &rect);
        SDL_FreeSurface(surface);
        SDL_DestroyTexture(texture);
    }
}

void interface_dessiner_piece(UIContext* ctx, int piece, int x, int y, int alpha) {
    if (piece == 0) return;
    
    int px = BOARD_OFFSET_X + x * TILE_SIZE;
    int py = BOARD_OFFSET_Y + y * TILE_SIZE;
    
    // we mirror jeu.c's piece codes here: r=king, n=queen, f=bishop, c=knight, t=rook, p=pawn
    int piece_idx = -1;
    int is_black = (piece < 0);
    char piece_type = abs(piece);

    switch(piece_type) {
        case 'r': piece_idx = 0; break;
        case 'n': piece_idx = 1; break;
        case 'f': piece_idx = 2; break;
        case 'c': piece_idx = 3; break;
        case 't': piece_idx = 4; break;
        case 'p': piece_idx = 5; break;
        default:
            break;
    }

    if (piece_idx < 0) {
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        int cx = px + TILE_SIZE / 2;
        int cy = py + TILE_SIZE / 2;
        SDL_SetRenderDrawColor(ctx->renderer, 255, 0, 0, alpha);
        SDL_Rect dot = {cx - 5, cy - 5, 10, 10};
        SDL_RenderFillRect(ctx->renderer, &dot);
        return;
    }
    
    SDL_Texture* texture = is_black ? ctx->black_pieces[piece_idx] : ctx->white_pieces[piece_idx];
    
    if (texture) {
        // we center the piece in the square with a small padding
        int piece_size = TILE_SIZE - 8;
        int offset = 4;
        SDL_Rect dst = {px + offset, py + offset, piece_size, piece_size};
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_RenderCopy(ctx->renderer, texture, NULL, &dst);
        SDL_SetTextureAlphaMod(texture, 255);
    } else {
        // we fall back to a colored circle with a letter when no texture is loaded
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        
        int cx = px + TILE_SIZE / 2;
        int cy = py + TILE_SIZE / 2;
        int radius = TILE_SIZE / 2 - 8;
        
        SDL_SetRenderDrawColor(ctx->renderer, 
            piece > 0 ? 255 : 50, 
            piece > 0 ? 255 : 50, 
            piece > 0 ? 255 : 50, alpha);
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy <= radius*radius) {
                    SDL_RenderDrawPoint(ctx->renderer, cx + dx, cy + dy);
                }
            }
        }
        
        char piece_char = '?';
        switch(piece_type) {
            case 'p': piece_char = 'P'; break;
            case 'c': piece_char = 'N'; break;
            case 'f': piece_char = 'B'; break;
            case 't': piece_char = 'R'; break;
            case 'n': piece_char = 'Q'; break;
            case 'r': piece_char = 'K'; break;
        }
        
        char text[2] = {piece_char, '\0'};
        SDL_Color color = {piece > 0 ? 0 : 255, piece > 0 ? 0 : 255, piece > 0 ? 0 : 255, 255};
        TTF_Font* font = ctx->font_medium;
        if (font) {
            SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
            if (surface) {
                SDL_Texture* txt = SDL_CreateTextureFromSurface(ctx->renderer, surface);
                SDL_Rect rect = {cx - surface->w/2, cy - surface->h/2, surface->w, surface->h};
                SDL_RenderCopy(ctx->renderer, txt, NULL, &rect);
                SDL_FreeSurface(surface);
                SDL_DestroyTexture(txt);
            }
        }
    }
}

void interface_dessiner_plateau(UIContext* ctx, struct config* conf) {
    if (ctx->bg_texture) {
        SDL_Rect bg_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        SDL_RenderCopy(ctx->renderer, ctx->bg_texture, NULL, &bg_rect);
    } else {
        interface_dessiner_rectangle(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 18, 20, 26, 255);
        interface_dessiner_rectangle(ctx, 0, 0, SCREEN_WIDTH, 180, 30, 34, 44, 255);
    }
    
    if (ctx->board_texture) {
        SDL_Rect board_rect = {BOARD_OFFSET_X, BOARD_OFFSET_Y, TILE_SIZE * 8, TILE_SIZE * 8};
        SDL_RenderCopy(ctx->renderer, ctx->board_texture, NULL, &board_rect);
    } else {
        interface_dessiner_rectangle(ctx, BOARD_OFFSET_X - 12, BOARD_OFFSET_Y - 12,
            TILE_SIZE * 8 + 24, TILE_SIZE * 8 + 24, 56, 42, 34, 255);

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                Uint8 r, g, b;
                if ((x + y) % 2 == 0) {
                    r = BOARD_LIGHT_R;
                    g = BOARD_LIGHT_G;
                    b = BOARD_LIGHT_B;
                } else {
                    r = BOARD_DARK_R;
                    g = BOARD_DARK_G;
                    b = BOARD_DARK_B;
                }
                interface_dessiner_case(ctx, x, y, r, g, b, 255);
            }
        }
    }
    
    if (ctx->last_move_sx >= 0) {
        interface_dessiner_case(ctx, ctx->last_move_sx, ctx->last_move_sy, 155, 199, 0, 100);
        interface_dessiner_case(ctx, ctx->last_move_dx, ctx->last_move_dy, 155, 199, 0, 100);
    }
    
    if (ctx->selected_x >= 0 && ctx->selected_y >= 0) {
        interface_dessiner_case(ctx, ctx->selected_x, ctx->selected_y, 106, 168, 79, 150);
    }
    
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (ctx->valid_moves[y][x]) {
                int cx = BOARD_OFFSET_X + x * TILE_SIZE + TILE_SIZE / 2;
                int cy = BOARD_OFFSET_Y + y * TILE_SIZE + TILE_SIZE / 2;
                int radius = conf->mat[y][x] ? TILE_SIZE / 2 - 4 : TILE_SIZE / 6;
                
                SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx->renderer, 100, 100, 100, 150);
                
                if (conf->mat[y][x]) {
                    // we draw a ring to mark a capture
                    for (int a = 0; a < 360; a += 5) {
                        float rad = a * 3.14159f / 180.0f;
                        int px = cx + (int)(radius * cosf(rad));
                        int py = cy + (int)(radius * sinf(rad));
                        SDL_RenderDrawPoint(ctx->renderer, px, py);
                    }
                } else {
                    // we draw a dot to mark a quiet move
                    SDL_Rect dot = {cx - radius/2, cy - radius/2, radius, radius};
                    SDL_RenderFillRect(ctx->renderer, &dot);
                }
            }
        }
    }

    for (int i = 0; i < 8; i++) {
        char file_label[2] = {(char)('a' + i), '\0'};
        char rank_label[2] = {(char)('8' - i), '\0'};
        int file_x = BOARD_OFFSET_X + i * TILE_SIZE + TILE_SIZE / 2;
        int top_y = BOARD_OFFSET_Y - 26;
        int bottom_y = BOARD_OFFSET_Y + TILE_SIZE * 8 + 8;
        int left_x = BOARD_OFFSET_X - 20;
        int right_x = BOARD_OFFSET_X + TILE_SIZE * 8 + 8;
        int rank_y = BOARD_OFFSET_Y + i * TILE_SIZE + TILE_SIZE / 2 - 8;

        interface_dessiner_texte_centre(ctx, file_label, file_x, top_y, 14, 230, 220, 190);
        interface_dessiner_texte_centre(ctx, file_label, file_x, bottom_y, 14, 230, 220, 190);
        interface_dessiner_texte(ctx, rank_label, left_x, rank_y, 14, 230, 220, 190);
        interface_dessiner_texte(ctx, rank_label, right_x, rank_y, 14, 230, 220, 190);
    }
}

void interface_dessiner_pieces(UIContext* ctx, struct config* conf) {
    int piece_count = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            char piece = conf->mat[y][x];
            if (piece != 0) {
                interface_dessiner_piece(ctx, piece, x, y, 255);
                piece_count++;
            }
        }
    }
    // we only log the piece count once, on the first render, for debugging
    static int first_draw = 1;
    if (first_draw) {
        printf("Drawing %d pieces on board\n", piece_count);
        first_draw = 0;
    }

    // we draw the dragged piece last so it renders on top
    if (ctx->dragging) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        interface_dessiner_piece(ctx, ctx->drag_piece, 
            (mx - BOARD_OFFSET_X) / TILE_SIZE,
            (my - BOARD_OFFSET_Y) / TILE_SIZE, 255);
    }
}

void interface_dessiner_panneau(UIContext* ctx, struct config* conf, int num_coup, char* dernier_coup, int player) {
    int white_pieces = 0;
    int black_pieces = 0;
    char buf[64];
    int panel_y = BOARD_OFFSET_Y;
    int section_y = panel_y + 24;

    compter_pieces(conf, &white_pieces, &black_pieces);

    interface_dessiner_rectangle(ctx, PANEL_X, panel_y, PANEL_WIDTH, TILE_SIZE * 8, 24, 27, 33, 236);
    interface_dessiner_rectangle(ctx, PANEL_X, panel_y, PANEL_WIDTH, 72, 42, 53, 68, 255);
    interface_dessiner_rectangle(ctx, PANEL_X + 20, panel_y + 92, PANEL_WIDTH - 40, 2, 88, 96, 110, 255);

    interface_dessiner_texte(ctx, "CHESS ENGINE", PANEL_X + 24, panel_y + 18, 28, 245, 225, 170);
    interface_dessiner_texte(ctx, "Alpha-Beta dashboard", PANEL_X + 24, panel_y + 52, 14, 180, 188, 200);

    interface_dessiner_texte(ctx, "POSITION", PANEL_X + 24, section_y + 84, 20, 255, 255, 255);
    snprintf(buf, sizeof(buf), "Move %d", num_coup);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 118, 14, 210, 210, 210);
    snprintf(buf, sizeof(buf), "Turn: %s", player == MAX ? "White" : "Black");
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 146, 14, 210, 210, 210);
    snprintf(buf, sizeof(buf), "Mode: %s", libelle_mode(ctx->game_mode));
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 174, 14, 210, 210, 210);
    snprintf(buf, sizeof(buf), "Last move: %s", (dernier_coup && dernier_coup[0]) ? dernier_coup : "--");
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 202, 14, 152, 210, 152);

    interface_dessiner_texte(ctx, "MATERIAL", PANEL_X + 24, section_y + 258, 20, 255, 255, 255);
    snprintf(buf, sizeof(buf), "White pieces: %d", white_pieces);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 292, 14, 220, 220, 220);
    snprintf(buf, sizeof(buf), "Black pieces: %d", black_pieces);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 320, 14, 220, 220, 220);
    snprintf(buf, sizeof(buf), "Eval: %+d", conf->val);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 348, 14,
        conf->val >= 0 ? 126 : 224,
        conf->val >= 0 ? 208 : 128,
        112);

    interface_dessiner_texte(ctx, "SEARCH", PANEL_X + 24, section_y + 404, 20, 255, 255, 255);
    snprintf(buf, sizeof(buf), "Depth: %d", ctx->depth);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 438, 14, 210, 210, 210);
    if (ctx->width == 0) snprintf(buf, sizeof(buf), "Width: all moves");
    else snprintf(buf, sizeof(buf), "Width: %d", ctx->width);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 466, 14, 210, 210, 210);
    snprintf(buf, sizeof(buf), "White eval: %s", libelle_heuristique(ctx->est_white));
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 494, 14, 210, 210, 210);
    snprintf(buf, sizeof(buf), "Black eval: %s", libelle_heuristique(ctx->est_black));
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 522, 14, 210, 210, 210);

    interface_dessiner_texte(ctx, "RUNTIME", PANEL_X + 24, section_y + 566, 20, 255, 255, 255);
    snprintf(buf, sizeof(buf), "OpenMP threads: %d", ctx->omp_threads);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 600, 14, 190, 190, 190);
    snprintf(buf, sizeof(buf), "Processors: %d", ctx->omp_procs);
    interface_dessiner_texte(ctx, buf, PANEL_X + 24, section_y + 628, 14, 190, 190, 190);
}

void interface_dessiner_menu(UIContext* ctx) {
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    interface_dessiner_rectangle(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 14, 18, 24, 255);
    interface_dessiner_rectangle(ctx, 0, 0, SCREEN_WIDTH, 228, 26, 32, 42, 255);
    interface_dessiner_rectangle(ctx, 72, 92, SCREEN_WIDTH - 144, 2, 126, 109, 74, 255);
    interface_dessiner_rectangle(ctx, 76, 0, 220, SCREEN_HEIGHT, 18, 22, 29, 80);
    interface_dessiner_rectangle(ctx, SCREEN_WIDTH - 296, 0, 220, SCREEN_HEIGHT, 18, 22, 29, 80);

    interface_dessiner_texte(ctx, "CHESS ENGINE", 118, 62, 28, 246, 221, 155);
    interface_dessiner_texte(ctx, "Graphical front end for the alpha-beta chess engine", 118, 100, 20, 188, 194, 204);
    interface_dessiner_texte(ctx, "Choose a mode on the left. Tune search settings on the right.", 118, 136, 14, 154, 160, 172);

    interface_dessiner_rectangle(ctx, 84, 214, 478, 420, 24, 28, 34, 230);
    interface_dessiner_rectangle(ctx, 84, 214, 478, 8, 232, 198, 108, 255);
    interface_dessiner_texte(ctx, "GAME MODES", 112, 236, 20, 255, 255, 255);
    interface_dessiner_texte(ctx, "Click a card to start immediately", 112, 264, 14, 170, 180, 190);

    const char* options[] = {
        "PC vs PC",
        "Human vs PC",
        "Human vs PC",
        "Quit"
    };
    const char* subtitles[] = {
        "Watch both sides search and play automatically",
        "You play Black. White is controlled by the engine",
        "You play White. Black is controlled by the engine",
        "Close the SDL client"
    };

    for (int i = 0; i < 4; i++) {
        int card_x = 112;
        int card_y = 298 + i * 82;
        int card_w = 422;
        int card_h = 66;
        int hovered = point_dans_rectangle(mouse_x, mouse_y, card_x, card_y, card_w, card_h);
        interface_dessiner_carte_mode(ctx, card_x, card_y, card_w, card_h, options[i], subtitles[i],
            (int)ctx->selected_option == i, hovered);
    }

    interface_dessiner_rectangle(ctx, 590, 214, 446, 420, 24, 28, 34, 230);
    interface_dessiner_rectangle(ctx, 590, 214, 446, 8, 108, 168, 128, 255);
    interface_dessiner_texte(ctx, "SEARCH SETUP", 618, 236, 20, 255, 255, 255);
    interface_dessiner_texte(ctx, "Mouse and keyboard controls are both enabled", 618, 264, 14, 170, 180, 190);

    interface_dessiner_rectangle(ctx, 618, 298, 390, 70, 35, 42, 52, 255);
    interface_dessiner_rectangle(ctx, 618, 380, 390, 70, 35, 42, 52, 255);
    interface_dessiner_rectangle(ctx, 618, 462, 390, 70, 35, 42, 52, 255);
    interface_dessiner_rectangle(ctx, 618, 544, 390, 70, 35, 42, 52, 255);

    char buf[96];
    interface_dessiner_texte(ctx, "White heuristic", 642, 312, 14, 255, 255, 255);
    snprintf(buf, sizeof(buf), "%s", libelle_heuristique(ctx->est_white));
    interface_dessiner_texte(ctx, buf, 642, 336, 20, 210, 218, 226);

    interface_dessiner_texte(ctx, "Black heuristic", 642, 394, 14, 255, 255, 255);
    snprintf(buf, sizeof(buf), "%s", libelle_heuristique(ctx->est_black));
    interface_dessiner_texte(ctx, buf, 642, 418, 20, 210, 218, 226);

    interface_dessiner_texte(ctx, "Search depth", 642, 476, 14, 255, 255, 255);
    snprintf(buf, sizeof(buf), "%d plies", ctx->depth);
    interface_dessiner_texte(ctx, buf, 642, 500, 20, 210, 218, 226);

    interface_dessiner_texte(ctx, "Beam width", 642, 558, 14, 255, 255, 255);
    if (ctx->width == 0) snprintf(buf, sizeof(buf), "All moves");
    else snprintf(buf, sizeof(buf), "%d moves", ctx->width);
    interface_dessiner_texte(ctx, buf, 642, 582, 20, 210, 218, 226);

    interface_dessiner_controle_menu(ctx, 930, 314, 32, 32, "-", point_dans_rectangle(mouse_x, mouse_y, 930, 314, 32, 32));
    interface_dessiner_controle_menu(ctx, 968, 314, 32, 32, "+", point_dans_rectangle(mouse_x, mouse_y, 968, 314, 32, 32));
    interface_dessiner_controle_menu(ctx, 930, 396, 32, 32, "-", point_dans_rectangle(mouse_x, mouse_y, 930, 396, 32, 32));
    interface_dessiner_controle_menu(ctx, 968, 396, 32, 32, "+", point_dans_rectangle(mouse_x, mouse_y, 968, 396, 32, 32));
    interface_dessiner_controle_menu(ctx, 930, 478, 32, 32, "-", point_dans_rectangle(mouse_x, mouse_y, 930, 478, 32, 32));
    interface_dessiner_controle_menu(ctx, 968, 478, 32, 32, "+", point_dans_rectangle(mouse_x, mouse_y, 968, 478, 32, 32));
    interface_dessiner_controle_menu(ctx, 930, 560, 32, 32, "-", point_dans_rectangle(mouse_x, mouse_y, 930, 560, 32, 32));
    interface_dessiner_controle_menu(ctx, 968, 560, 32, 32, "+", point_dans_rectangle(mouse_x, mouse_y, 968, 560, 32, 32));
    interface_dessiner_controle_menu(ctx, 894, 560, 96, 32, "Reset", point_dans_rectangle(mouse_x, mouse_y, 894, 560, 96, 32));

    interface_dessiner_rectangle(ctx, 84, 654, 952, 54, 23, 26, 32, 215);
    interface_dessiner_texte(ctx, "Keyboard: Up/Down modes, Q/A white, W/S black, Left/Right depth, E/D width, R reset", 112, 672, 14, 160, 170, 180);
}

void interface_dessiner_fin_partie(UIContext* ctx, int winner) {
    interface_dessiner_rectangle(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 180);

    interface_dessiner_rectangle(ctx, SCREEN_WIDTH / 2 - 220, SCREEN_HEIGHT / 2 - 110, 440, 180, 20, 24, 30, 235);
    interface_dessiner_rectangle(ctx, SCREEN_WIDTH / 2 - 220, SCREEN_HEIGHT / 2 - 110, 440, 8, 232, 198, 108, 255);
    const char* msg = winner == MAX ? "WHITE WINS!" : (winner == MIN ? "BLACK WINS!" : "DRAW!");
    interface_dessiner_texte_centre(ctx, msg, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 34, 28, 255, 215, 120);
    interface_dessiner_texte_centre(ctx, "Press Enter to return to the menu", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 18, 20, 205, 205, 205);
}

void interface_dessiner_promotion(UIContext* ctx, int is_white) {
    interface_dessiner_rectangle(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 200);

    int box_w = 320, box_h = 100;
    int box_x = SCREEN_WIDTH/2 - box_w/2;
    int box_y = SCREEN_HEIGHT/2 - box_h/2;
    
    interface_dessiner_rectangle(ctx, box_x, box_y, box_w, box_h, 60, 60, 80, 255);
    interface_dessiner_texte(ctx, "Choose promotion:", box_x + 80, box_y + 15, 20, 255, 255, 255);
    
    const char* options[] = {"Queen", "Rook", "Bishop", "Knight"};
    const int values[] = {'n', 't', 'f', 'c'};
    
    for (int i = 0; i < 4; i++) {
        int opt_x = box_x + 16 + i * 72;
        int opt_y = box_y + 46;
        int selected = (is_white && ctx->promotion_choice == values[i]) ||
                       (!is_white && ctx->promotion_choice == -values[i]);

        interface_dessiner_rectangle(ctx, opt_x, opt_y, 64, 40,
            selected ? 100 : 48,
            selected ? 150 : 56,
            selected ? 100 : 72,
            255);
        interface_dessiner_texte(ctx, options[i], opt_x + 6, opt_y + 11, 14, 255, 255, 255);
    }

    interface_dessiner_texte(ctx, "Click an option or press 1-4 / Q,R,B,N", box_x + 26, box_y + 84, 14, 210, 210, 210);
}
