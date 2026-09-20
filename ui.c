#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* heuristic_labels[] = {
    "1 Material",
    "2 Center+King",
    "3 Material+Noise",
    "4 Threats",
    "5 Center",
    "6 Combined",
    "7 Random"
};

// short names for the side panel, long ones for the menu
static const char* algo_labels[RECHERCHE_COUNT] = {"SEQUENTIAL", "ROOT SPLIT", "YBW TREE", "LAZY SMP"};
static const char* algo_names[RECHERCHE_COUNT] = {"Sequential", "Root split", "YBW tree split", "Lazy SMP"};

const char* interface_nom_algo(TypeRecherche algo) {
    return (algo >= 0 && algo < RECHERCHE_COUNT) ? algo_labels[algo] : "?";
}

static const Uint32 heat_ramp[] = {PAL_HEAT_0, PAL_HEAT_1, PAL_HEAT_2, PAL_HEAT_3, PAL_HEAT_4};

static const char* display_font_paths[] = {
    "fonts/ClashDisplay-Semibold.ttf",
    "C:/Windows/Fonts/seguisb.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"
};

static const char* display_medium_font_paths[] = {
    "fonts/ClashDisplay-Medium.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf"
};

static const char* mono_font_paths[] = {
    "fonts/FragmentMono-Regular.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
};

#define NB_ELEMENTS(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

#define PIECE_SPRITE_SIZE 20

#define MENU_CARD_X 112
#define MENU_CARD_Y 272
#define MENU_CARD_W 422
#define MENU_CARD_H 66
#define MENU_CARD_STEP 80

#define MENU_ROW_X 618
#define MENU_ROW_Y 272
#define MENU_ROW_W 390
#define MENU_ROW_H 50
#define MENU_ROW_STEP 56
#define MENU_CTRL_SIZE 32
#define MENU_CTRL_MINUS_X 930
#define MENU_CTRL_PLUS_X 968
#define MENU_RESET_X 862
#define MENU_RESET_W 60
#define MENU_SEG_W 56
#define MENU_SEG_X (MENU_ROW_X + MENU_ROW_W - 8 - 2 * MENU_SEG_W - 4)

#define PROMO_BOX_W 380
#define PROMO_BOX_H 204
#define PROMO_TILE 76
#define PROMO_GAP 12

static TTF_Font* ouvrir_police(const char* const* paths, int count, int size) {
    for (int i = 0; i < count; i++) {
        TTF_Font* font = TTF_OpenFont(paths[i], size);
        if (font) {
            TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
            return font;
        }
    }
    printf("  No font found for size %d\n", size);
    return NULL;
}

static TTF_Font* police_pour_taille(UIContext* ctx, int size) {
    switch (size) {
        case FONT_TITLE: return ctx->font_title;
        case FONT_LARGE: return ctx->font_large;
        case FONT_SMALL: return ctx->font_small;
        case FONT_TINY: return ctx->font_tiny;
        default: return ctx->font_medium;
    }
}

static int largeur_texte(UIContext* ctx, const char* text, int size) {
    TTF_Font* font = police_pour_taille(ctx, size);
    int text_w = 0;
    int text_h = 0;
    if (!font || TTF_SizeUTF8(font, text, &text_w, &text_h) != 0) return 0;
    return text_w;
}

static void texte(UIContext* ctx, const char* text, int x, int y, int size, Uint32 hex) {
    interface_dessiner_texte(ctx, text, x, y, size, PAL_R(hex), PAL_G(hex), PAL_B(hex));
}

static void texte_centre(UIContext* ctx, const char* text, int center_x, int y, int size, Uint32 hex) {
    texte(ctx, text, center_x - largeur_texte(ctx, text, size) / 2, y, size, hex);
}

static void texte_droite(UIContext* ctx, const char* text, int right_x, int y, int size, Uint32 hex) {
    texte(ctx, text, right_x - largeur_texte(ctx, text, size), y, size, hex);
}

static void remplir(UIContext* ctx, int x, int y, int w, int h, Uint32 hex, Uint8 a) {
    interface_dessiner_rectangle(ctx, x, y, w, h, PAL_R(hex), PAL_G(hex), PAL_B(hex), a);
}

static void rectangle_arrondi(UIContext* ctx, int x, int y, int w, int h, int radius, Uint32 hex, Uint8 a) {
    if (radius * 2 > h) radius = h / 2;
    if (radius * 2 > w) radius = w / 2;

    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, PAL_R(hex), PAL_G(hex), PAL_B(hex), a);

    for (int row = 0; row < h; row++) {
        int inset = 0;
        int dist = -1;
        if (row < radius) dist = radius - row;
        else if (row >= h - radius) dist = row - (h - radius) + 1;

        if (dist > 0) {
            float dy = (float)dist - 0.5f;
            inset = radius - (int)floorf(sqrtf((float)(radius * radius) - dy * dy) + 0.5f);
        }

        SDL_Rect line = {x + inset, y + row, w - inset * 2, 1};
        SDL_RenderFillRect(ctx->renderer, &line);
    }
}

static void carte(UIContext* ctx, int x, int y, int w, int h, int radius, Uint32 fill, Uint32 border) {
    rectangle_arrondi(ctx, x, y, w, h, radius, border, 255);
    rectangle_arrondi(ctx, x + 1, y + 1, w - 2, h - 2, radius > 0 ? radius - 1 : 0, fill, 255);
}

static void disque(UIContext* ctx, int cx, int cy, int radius, Uint32 hex, Uint8 a) {
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, PAL_R(hex), PAL_G(hex), PAL_B(hex), a);

    for (int dy = -radius; dy <= radius; dy++) {
        int half = (int)sqrtf((float)(radius * radius - dy * dy));
        SDL_Rect line = {cx - half, cy + dy, half * 2 + 1, 1};
        SDL_RenderFillRect(ctx->renderer, &line);
    }
}

static void anneau(UIContext* ctx, int cx, int cy, int radius, int thickness, Uint32 hex, Uint8 a) {
    int inner = radius - thickness;

    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, PAL_R(hex), PAL_G(hex), PAL_B(hex), a);

    for (int dy = -radius; dy <= radius; dy++) {
        int outer_half = (int)sqrtf((float)(radius * radius - dy * dy));

        if (abs(dy) < inner) {
            int inner_half = (int)sqrtf((float)(inner * inner - dy * dy));
            SDL_Rect left = {cx - outer_half, cy + dy, outer_half - inner_half, 1};
            SDL_Rect right = {cx + inner_half + 1, cy + dy, outer_half - inner_half, 1};
            SDL_RenderFillRect(ctx->renderer, &left);
            SDL_RenderFillRect(ctx->renderer, &right);
        } else {
            SDL_Rect line = {cx - outer_half, cy + dy, outer_half * 2 + 1, 1};
            SDL_RenderFillRect(ctx->renderer, &line);
        }
    }
}

static Uint32 melanger(Uint32 from, Uint32 to, float t) {
    Uint8 r = (Uint8)(PAL_R(from) + (PAL_R(to) - PAL_R(from)) * t);
    Uint8 g = (Uint8)(PAL_G(from) + (PAL_G(to) - PAL_G(from)) * t);
    Uint8 b = (Uint8)(PAL_B(from) + (PAL_B(to) - PAL_B(from)) * t);
    return ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}

static void degrade_chaleur(UIContext* ctx, int x, int y, int w, int h) {
    int stops = NB_ELEMENTS(heat_ramp) - 1;

    for (int i = 0; i < w; i++) {
        float pos = (float)i / (float)(w - 1) * stops;
        int seg = (int)pos;
        if (seg >= stops) seg = stops - 1;
        remplir(ctx, x + i, y, 1, h, melanger(heat_ramp[seg], heat_ramp[seg + 1], pos - seg), 255);
    }
}

static void separateur(UIContext* ctx, int x, int y, int w) {
    remplir(ctx, x, y, w, 1, PAL_BORDER, 255);
}

static void ligne_cle_valeur(UIContext* ctx, const char* key, const char* value, int x, int right_x, int y,
    Uint32 value_color) {
    texte(ctx, key, x, y, FONT_SMALL, PAL_MUTED);
    texte_droite(ctx, value, right_x, y, FONT_SMALL, value_color);
}

static int point_dans_rectangle(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void interface_dessiner_controle_menu(UIContext* ctx, int x, int y, int w, int h, const char* label,
    int hovered, int active) {
    Uint32 border = hovered ? PAL_ACCENT : PAL_BORDER;
    Uint32 fill = active ? PAL_HEAT_1 : PAL_SURFACE;
    Uint32 ink = (hovered || active) ? PAL_ACCENT : PAL_FOREGROUND;

    carte(ctx, x, y, w, h, 6, fill, border);
    texte_centre(ctx, label, x + w / 2, y + (h - TTF_FontHeight(ctx->font_small)) / 2, FONT_SMALL, ink);
}

static void chevron(UIContext* ctx, int x, int cy, int size, Uint32 hex) {
    for (int i = 0; i < size; i++) {
        remplir(ctx, x + i, cy - (size - i), 1, (size - i) * 2, hex, 255);
    }
}

static void interface_dessiner_carte_mode(UIContext* ctx, int index, int x, int y, int w, int h, const char* title,
    const char* subtitle, int selected, int hovered) {
    Uint32 highlight = (index == MENU_QUIT) ? PAL_WARM : PAL_ACCENT;
    Uint32 border = selected ? highlight : (hovered ? PAL_HEAT_1 : PAL_BORDER);
    Uint32 fill = (selected || hovered) ? PAL_HEAT_0 : PAL_SURFACE;
    char number[8];

    carte(ctx, x, y, w, h, 8, fill, border);
    if (selected) remplir(ctx, x + 1, y + 14, 3, h - 28, highlight, 255);

    snprintf(number, sizeof(number), "%02d", index + 1);
    texte(ctx, number, x + 22, y + 24, FONT_SMALL, selected ? highlight : PAL_MUTED);
    texte(ctx, title, x + 64, y + 10, FONT_MEDIUM, PAL_FOREGROUND);
    texte(ctx, subtitle, x + 64, y + 40, FONT_TINY, PAL_MUTED);

    if (selected || hovered) chevron(ctx, x + w - 30, y + h / 2, 6, selected ? highlight : PAL_MUTED);
}

static const char* libelle_heuristique(int heuristic) {
    if (heuristic >= 0 && heuristic < NB_ELEMENTS(heuristic_labels)) {
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

static int indice_texture_piece(char piece_type) {
    switch (piece_type) {
        case 'r': return 0;
        case 'n': return 1;
        case 'f': return 2;
        case 'c': return 3;
        case 't': return 4;
        case 'p': return 5;
        default: return -1;
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

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    ctx->window = SDL_CreateWindow("Chess Engine - Alpha Beta",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

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
    SDL_RenderSetLogicalSize(ctx->renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    ctx->font_title = ouvrir_police(display_font_paths, NB_ELEMENTS(display_font_paths), FONT_TITLE);
    ctx->font_large = ouvrir_police(display_font_paths, NB_ELEMENTS(display_font_paths), FONT_LARGE);
    ctx->font_medium = ouvrir_police(display_medium_font_paths, NB_ELEMENTS(display_medium_font_paths), FONT_MEDIUM);
    ctx->font_small = ouvrir_police(mono_font_paths, NB_ELEMENTS(mono_font_paths), FONT_SMALL);
    ctx->font_tiny = ouvrir_police(mono_font_paths, NB_ELEMENTS(mono_font_paths), FONT_TINY);

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
    ctx->est_white = 5;
    ctx->est_black = 5;
    ctx->depth = 5;
    ctx->width = 0;
    ctx->algo_white = RECHERCHE_YBW;
    ctx->algo_black = RECHERCHE_YBW;
    ctx->benchmark = 0;
    ctx->thinking = 0;
    ctx->thinking_seconds = 0.0;
    memset(ctx->last_search, 0, sizeof(ctx->last_search));
    memset(&ctx->bench, 0, sizeof(ctx->bench));
    ctx->show_message = 0;
    ctx->promotion_choice = 'n';

    for (int i = 0; i < 6; i++) {
        ctx->white_pieces[i] = NULL;
        ctx->black_pieces[i] = NULL;
    }

    memset(ctx->valid_moves, 0, sizeof(ctx->valid_moves));

    return 1;
}

void interface_nettoyer(UIContext* ctx) {
    for (int i = 0; i < 6; i++) {
        if (ctx->white_pieces[i]) SDL_DestroyTexture(ctx->white_pieces[i]);
        if (ctx->black_pieces[i]) SDL_DestroyTexture(ctx->black_pieces[i]);
    }

    if (ctx->font_title) TTF_CloseFont(ctx->font_title);
    if (ctx->font_large) TTF_CloseFont(ctx->font_large);
    if (ctx->font_medium) TTF_CloseFont(ctx->font_medium);
    if (ctx->font_small) TTF_CloseFont(ctx->font_small);
    if (ctx->font_tiny) TTF_CloseFont(ctx->font_tiny);
    if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window) SDL_DestroyWindow(ctx->window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void interface_charger_pieces(UIContext* ctx) {
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
                SDL_SetTextureScaleMode(ctx->white_pieces[i], SDL_ScaleModeNearest);
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
                SDL_SetTextureScaleMode(ctx->black_pieces[i], SDL_ScaleModeNearest);
                printf("  Loaded: %s\n", black_piece_files[i]);
            } else {
                printf("  Failed to create texture: %s\n", black_piece_files[i]);
            }
        } else {
            printf("  Failed to load: %s - %s\n", black_piece_files[i], IMG_GetError());
        }
    }
}

void interface_effacer_ecran(UIContext* ctx) {
    SDL_SetRenderDrawColor(ctx->renderer, PAL_R(PAL_BACKGROUND), PAL_G(PAL_BACKGROUND), PAL_B(PAL_BACKGROUND), 255);
    SDL_RenderClear(ctx->renderer);
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

static void surligner_case(UIContext* ctx, int x, int y, Uint32 hex, Uint8 a) {
    interface_dessiner_case(ctx, x, y, PAL_R(hex), PAL_G(hex), PAL_B(hex), a);
}

void interface_dessiner_texte(UIContext* ctx, const char* text, int x, int y, int size, Uint8 r, Uint8 g, Uint8 b) {
    TTF_Font* font = police_pour_taille(ctx, size);

    if (!font || !text || !text[0]) return;

    SDL_Color color = {r, g, b, 255};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(ctx->renderer, surface);
        SDL_Rect rect = {x, y, surface->w, surface->h};
        SDL_RenderCopy(ctx->renderer, texture, NULL, &rect);
        SDL_FreeSurface(surface);
        SDL_DestroyTexture(texture);
    }
}

static void dessiner_texture_piece(UIContext* ctx, int piece, int px, int py, int size, int alpha) {
    int piece_idx = indice_texture_piece((char)abs(piece));
    SDL_Texture* texture = NULL;

    if (piece_idx >= 0) {
        texture = (piece < 0) ? ctx->black_pieces[piece_idx] : ctx->white_pieces[piece_idx];
    }

    if (texture) {
        int tex_w = 0;
        int tex_h = 0;
        int scale = size / PIECE_SPRITE_SIZE;
        if (scale < 1) scale = 1;
        SDL_QueryTexture(texture, NULL, NULL, &tex_w, &tex_h);

        int cell = PIECE_SPRITE_SIZE * scale;
        int base_y = py + (size + cell) / 2;
        SDL_Rect dst = {px + (size - tex_w * scale) / 2, base_y - tex_h * scale, tex_w * scale, tex_h * scale};
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_RenderCopy(ctx->renderer, texture, NULL, &dst);
        SDL_SetTextureAlphaMod(texture, 255);
        return;
    }

    char piece_char = '?';
    switch (abs(piece)) {
        case 'p': piece_char = 'P'; break;
        case 'c': piece_char = 'N'; break;
        case 'f': piece_char = 'B'; break;
        case 't': piece_char = 'R'; break;
        case 'n': piece_char = 'Q'; break;
        case 'r': piece_char = 'K'; break;
    }

    int cx = px + size / 2;
    int cy = py + size / 2;
    char text[2] = {piece_char, '\0'};
    Uint32 fill = piece > 0 ? PAL_FOREGROUND : PAL_BACKGROUND;
    Uint32 ink = piece > 0 ? PAL_BACKGROUND : PAL_FOREGROUND;

    disque(ctx, cx, cy, size / 2 - 4, fill, (Uint8)alpha);
    texte_centre(ctx, text, cx, cy - TTF_FontHeight(ctx->font_medium) / 2, FONT_MEDIUM, ink);
}

void interface_dessiner_piece(UIContext* ctx, int piece, int x, int y, int alpha) {
    if (piece == 0) return;

    // we center the piece in the square with a small padding
    int px = BOARD_OFFSET_X + x * TILE_SIZE + 4;
    int py = BOARD_OFFSET_Y + y * TILE_SIZE + 4;
    dessiner_texture_piece(ctx, piece, px, py, TILE_SIZE - 8, alpha);
}

void interface_dessiner_plateau(UIContext* ctx, struct config* conf) {
    int board_px = TILE_SIZE * BOARD_SIZE;
    int frame = 28;

    remplir(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, PAL_BACKGROUND, 255);
    carte(ctx, BOARD_OFFSET_X - frame, BOARD_OFFSET_Y - frame, board_px + frame * 2, board_px + frame * 2,
        12, PAL_SURFACE, PAL_BORDER);

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            surligner_case(ctx, x, y, ((x + y) % 2 == 0) ? BOARD_LIGHT : BOARD_DARK, 255);
        }
    }

    if (ctx->last_move_sx >= 0) {
        surligner_case(ctx, ctx->last_move_sx, ctx->last_move_sy, PAL_ACCENT, 90);
        surligner_case(ctx, ctx->last_move_dx, ctx->last_move_dy, PAL_ACCENT, 140);
    }

    if (ctx->selected_x >= 0 && ctx->selected_y >= 0) {
        int px = BOARD_OFFSET_X + ctx->selected_x * TILE_SIZE;
        int py = BOARD_OFFSET_Y + ctx->selected_y * TILE_SIZE;
        surligner_case(ctx, ctx->selected_x, ctx->selected_y, PAL_ACCENT, 170);
        remplir(ctx, px, py, TILE_SIZE, 3, PAL_BACKGROUND, 120);
        remplir(ctx, px, py + TILE_SIZE - 3, TILE_SIZE, 3, PAL_BACKGROUND, 120);
        remplir(ctx, px, py + 3, 3, TILE_SIZE - 6, PAL_BACKGROUND, 120);
        remplir(ctx, px + TILE_SIZE - 3, py + 3, 3, TILE_SIZE - 6, PAL_BACKGROUND, 120);
    }

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (!ctx->valid_moves[y][x]) continue;

            int cx = BOARD_OFFSET_X + x * TILE_SIZE + TILE_SIZE / 2;
            int cy = BOARD_OFFSET_Y + y * TILE_SIZE + TILE_SIZE / 2;

            if (conf->mat[y][x]) {
                // we draw a ring to mark a capture
                anneau(ctx, cx, cy, TILE_SIZE / 2 - 3, 5, PAL_WARM, 220);
            } else {
                // we draw a dot to mark a quiet move
                disque(ctx, cx, cy, TILE_SIZE / 8, PAL_BACKGROUND, 110);
            }
        }
    }

    for (int i = 0; i < 8; i++) {
        char file_label[2] = {(char)('a' + i), '\0'};
        char rank_label[2] = {(char)('8' - i), '\0'};
        int label_h = ctx->font_tiny ? TTF_FontHeight(ctx->font_tiny) : 12;
        int file_x = BOARD_OFFSET_X + i * TILE_SIZE + TILE_SIZE / 2;
        int bottom_y = BOARD_OFFSET_Y + board_px + (frame - label_h) / 2;
        int rank_y = BOARD_OFFSET_Y + i * TILE_SIZE + (TILE_SIZE - label_h) / 2;

        texte_centre(ctx, file_label, file_x, bottom_y, FONT_TINY, PAL_MUTED);
        texte_centre(ctx, rank_label, BOARD_OFFSET_X - frame / 2, rank_y, FONT_TINY, PAL_MUTED);
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

static void barre_evaluation(UIContext* ctx, int x, int y, int w, int h, int value) {
    int clamped = value;
    int center = x + w / 2;

    if (clamped > 100) clamped = 100;
    if (clamped < -100) clamped = -100;

    rectangle_arrondi(ctx, x, y, w, h, h / 2, PAL_HEAT_0, 255);

    int span = (w / 2) * abs(clamped) / 100;
    if (clamped > 0) remplir(ctx, center, y + 2, span, h - 4, PAL_POSITIVE, 255);
    else if (clamped < 0) remplir(ctx, center - span, y + 2, span, h - 4, PAL_NEGATIVE, 255);

    remplir(ctx, center, y - 3, 1, h + 6, PAL_FOREGROUND, 200);
}

static void pastilles_materiel(UIContext* ctx, int x, int y, int count, Uint32 hex) {
    for (int i = 0; i < 16; i++) {
        rectangle_arrondi(ctx, x + i * 13, y, 9, 9, 2, i < count ? hex : PAL_HEAT_0, 255);
    }
}

static void formater_noeuds(char* buf, size_t size, long long nodes) {
    if (nodes >= 10000000LL) snprintf(buf, size, "%.0fM", nodes / 1e6);
    else if (nodes >= 1000000LL) snprintf(buf, size, "%.1fM", nodes / 1e6);
    else if (nodes >= 10000LL) snprintf(buf, size, "%.0fk", nodes / 1e3);
    else snprintf(buf, size, "%lld", nodes);
}

static void formater_duree(char* buf, size_t size, double seconds) {
    if (seconds < 1.0) snprintf(buf, size, "%.0f ms", seconds * 1000.0);
    else snprintf(buf, size, "%.2f s", seconds);
}

static int camp_humain(UIContext* ctx, int side) {
    return (side == MAX && ctx->game_mode == 3) || (side == MIN && ctx->game_mode == 2);
}

static void colonne_moteur(UIContext* ctx, int side, int x, int y) {
    int is_white = (side == MAX);
    Uint32 color = is_white ? PAL_POSITIVE : PAL_NEGATIVE;
    TypeRecherche algo = is_white ? ctx->algo_white : ctx->algo_black;
    DerniereRecherche* last = &ctx->last_search[is_white ? 0 : 1];
    char buf[48];
    char nodes[24];

    texte(ctx, is_white ? "WHITE" : "BLACK", x, y, FONT_TINY, color);

    if (camp_humain(ctx, side)) {
        texte(ctx, "Human", x, y + 20, FONT_SMALL, PAL_FOREGROUND);
        return;
    }

    texte(ctx, libelle_heuristique(is_white ? ctx->est_white : ctx->est_black), x, y + 20, FONT_SMALL,
        PAL_FOREGROUND);
    if (algo == RECHERCHE_SEQUENTIELLE) snprintf(buf, sizeof(buf), "%s", interface_nom_algo(algo));
    else snprintf(buf, sizeof(buf), "%s x%d", interface_nom_algo(algo), ctx->omp_threads);
    texte(ctx, buf, x, y + 42, FONT_TINY, algo == RECHERCHE_SEQUENTIELLE ? PAL_SERIES_2 : PAL_ACCENT);

    if (last->valid) {
        formater_duree(buf, sizeof(buf), last->seconds);
        texte(ctx, buf, x, y + 62, FONT_SMALL, PAL_FOREGROUND);
        formater_noeuds(nodes, sizeof(nodes), last->nodes);
        snprintf(buf, sizeof(buf), "%s nodes", nodes);
        texte(ctx, buf, x, y + 82, FONT_TINY, PAL_MUTED);
    } else {
        texte(ctx, "--", x, y + 62, FONT_SMALL, PAL_MUTED);
    }
}

void interface_dessiner_panneau(UIContext* ctx, struct config* conf, int num_coup, char* dernier_coup, int player) {
    int white_pieces = 0;
    int black_pieces = 0;
    char buf[64];
    int panel_y = BOARD_OFFSET_Y - 28;
    int panel_h = TILE_SIZE * BOARD_SIZE + 56;
    int left = PANEL_X + 24;
    int right = PANEL_X + PANEL_WIDTH - 24;
    int inner_w = right - left;
    int y;
    Uint32 eval_color;

    compter_pieces(conf, &white_pieces, &black_pieces);

    carte(ctx, PANEL_X, panel_y, PANEL_WIDTH, panel_h, 12, PAL_SURFACE, PAL_BORDER);
    degrade_chaleur(ctx, PANEL_X + 12, panel_y + 1, PANEL_WIDTH - 24, 3);

    y = panel_y + 18;
    texte(ctx, "Chess Engine", left, y, FONT_LARGE, PAL_FOREGROUND);
    texte_droite(ctx, "menu", right, y + 12, FONT_TINY, PAL_MUTED);
    texte_droite(ctx, "ESC ", right - largeur_texte(ctx, "menu", FONT_TINY), y + 12, FONT_TINY, PAL_ACCENT);

    y = panel_y + 80;
    separateur(ctx, left, y, inner_w);

    y += 14;
    disque(ctx, left + 8, y + 13, 8, PAL_BORDER, 255);
    disque(ctx, left + 8, y + 13, 7, player == MAX ? PAL_FOREGROUND : PAL_BACKGROUND, 255);
    if (ctx->thinking) {
        snprintf(buf, sizeof(buf), "%s thinking \xC2\xB7 %.1f s", player == MAX ? "White" : "Black",
            ctx->thinking_seconds);
    } else {
        snprintf(buf, sizeof(buf), "%s to move", player == MAX ? "White" : "Black");
    }
    texte(ctx, buf, left + 26, y, FONT_MEDIUM, ctx->thinking ? PAL_ACCENT : PAL_FOREGROUND);

    y += 34;
    snprintf(buf, sizeof(buf), "%d", num_coup);
    ligne_cle_valeur(ctx, "MOVE", buf, left, right, y, PAL_FOREGROUND);
    ligne_cle_valeur(ctx, "LAST", (dernier_coup && dernier_coup[0]) ? dernier_coup : "--",
        left, right, y + 22, PAL_ACCENT);

    y += 52;
    separateur(ctx, left, y, inner_w);

    y += 14;
    eval_color = conf->val > 0 ? PAL_POSITIVE : (conf->val < 0 ? PAL_NEGATIVE : PAL_FOREGROUND);
    texte(ctx, "EVALUATION", left, y + 4, FONT_TINY, PAL_MUTED);
    snprintf(buf, sizeof(buf), "%+d", conf->val);
    texte_droite(ctx, buf, right, y - 4, FONT_MEDIUM, eval_color);
    barre_evaluation(ctx, left, y + 30, inner_w, 10, conf->val);

    y += 50;
    texte(ctx, "W", left, y, FONT_SMALL, PAL_POSITIVE);
    pastilles_materiel(ctx, left + 36, y + 5, white_pieces, PAL_POSITIVE);
    snprintf(buf, sizeof(buf), "%d", white_pieces);
    texte_droite(ctx, buf, right, y, FONT_SMALL, PAL_FOREGROUND);
    y += 22;
    texte(ctx, "B", left, y, FONT_SMALL, PAL_NEGATIVE);
    pastilles_materiel(ctx, left + 36, y + 5, black_pieces, PAL_NEGATIVE);
    snprintf(buf, sizeof(buf), "%d", black_pieces);
    texte_droite(ctx, buf, right, y, FONT_SMALL, PAL_FOREGROUND);

    y += 32;
    separateur(ctx, left, y, inner_w);

    y += 14;
    texte(ctx, "ENGINES", left, y, FONT_TINY, PAL_MUTED);
    if (ctx->width == 0) snprintf(buf, sizeof(buf), "DEPTH %d \xC2\xB7 ALL MOVES", ctx->depth);
    else snprintf(buf, sizeof(buf), "DEPTH %d \xC2\xB7 WIDTH %d", ctx->depth, ctx->width);
    texte_droite(ctx, buf, right, y, FONT_TINY, PAL_MUTED);
    y += 22;
    colonne_moteur(ctx, MAX, left, y);
    colonne_moteur(ctx, MIN, left + inner_w / 2 + 8, y);

    y += 108;
    separateur(ctx, left, y, inner_w);

    y += 14;
    texte(ctx, "BENCHMARK VS SEQUENTIAL", left, y, FONT_TINY, PAL_MUTED);
    if (!ctx->benchmark) {
        texte_droite(ctx, "OFF", right, y, FONT_TINY, PAL_MUTED);
        texte(ctx, "Enable it in the menu (B) to compare", left, y + 24, FONT_TINY, PAL_MUTED);
        texte(ctx, "every engine move against sequential.", left, y + 42, FONT_TINY, PAL_MUTED);
        return;
    }

    int col_speed = left + 150;
    int col_nodes = left + 222;
    y += 22;
    texte(ctx, "ALGO", left, y, FONT_TINY, PAL_MUTED);
    texte_droite(ctx, "SPEED", col_speed, y, FONT_TINY, PAL_MUTED);
    texte_droite(ctx, "NODES", col_nodes, y, FONT_TINY, PAL_MUTED);
    texte_droite(ctx, "SAME", right, y, FONT_TINY, PAL_MUTED);

    int rows = 0;
    for (int a = RECHERCHE_PARALLELE; a < RECHERCHE_COUNT; a++) {
        StatsBenchmark* bench = &ctx->bench[a];
        if (bench->compared == 0 || bench->algo_seconds <= 0.0 || bench->seq_nodes == 0) continue;

        double speedup = bench->seq_seconds / bench->algo_seconds;
        int all_same = bench->same_move == bench->compared && bench->same_score == bench->compared;

        y += 22;
        rows++;
        texte(ctx, interface_nom_algo((TypeRecherche)a), left, y, FONT_SMALL, PAL_FOREGROUND);
        snprintf(buf, sizeof(buf), "%.2fx", speedup);
        texte_droite(ctx, buf, col_speed, y, FONT_SMALL, speedup >= 1.0 ? PAL_ACCENT : PAL_WARM);
        snprintf(buf, sizeof(buf), "%.1fx", (double)bench->algo_nodes / (double)bench->seq_nodes);
        texte_droite(ctx, buf, col_nodes, y, FONT_SMALL, PAL_FOREGROUND);
        snprintf(buf, sizeof(buf), "%d/%d", bench->same_move, bench->compared);
        texte_droite(ctx, buf, right, y, FONT_SMALL, all_same ? PAL_SERIES_3 : PAL_WARM);
    }

    if (rows == 0) {
        texte(ctx, "Waiting for an engine move that uses", left, y + 24, FONT_TINY, PAL_MUTED);
        texte(ctx, "a parallel algorithm...", left, y + 42, FONT_TINY, PAL_MUTED);
    }
}

static int dessiner_raccourci(UIContext* ctx, const char* keys, const char* action, int x, int y) {
    texte(ctx, keys, x, y, FONT_TINY, PAL_ACCENT);
    x += largeur_texte(ctx, keys, FONT_TINY) + 6;
    texte(ctx, action, x, y, FONT_TINY, PAL_MUTED);
    return x + largeur_texte(ctx, action, FONT_TINY) + 20;
}

static int ligne_bascule(int row) {
    return row == MENU_ROW_BENCHMARK;
}

static int rectangle_controle(int row, int control, SDL_Rect* rect) {
    int y = MENU_ROW_Y + row * MENU_ROW_STEP + (MENU_ROW_H - MENU_CTRL_SIZE) / 2;

    if (ligne_bascule(row)) {
        if (control == MENU_CTRL_ALL) return 0;
        *rect = (SDL_Rect){MENU_SEG_X + control * (MENU_SEG_W + 4), y, MENU_SEG_W, MENU_CTRL_SIZE};
        return 1;
    }

    switch (control) {
        case MENU_CTRL_DECREMENT:
            *rect = (SDL_Rect){MENU_CTRL_MINUS_X, y, MENU_CTRL_SIZE, MENU_CTRL_SIZE};
            return 1;
        case MENU_CTRL_INCREMENT:
            *rect = (SDL_Rect){MENU_CTRL_PLUS_X, y, MENU_CTRL_SIZE, MENU_CTRL_SIZE};
            return 1;
        default:
            if (row != MENU_ROW_WIDTH) return 0;
            *rect = (SDL_Rect){MENU_RESET_X, y, MENU_RESET_W, MENU_CTRL_SIZE};
            return 1;
    }
}

int interface_menu_carte(int mx, int my) {
    for (int i = 0; i < 4; i++) {
        if (point_dans_rectangle(mx, my, MENU_CARD_X, MENU_CARD_Y + i * MENU_CARD_STEP, MENU_CARD_W, MENU_CARD_H)) {
            return i;
        }
    }
    return -1;
}

int interface_menu_controle(int mx, int my, int* row, int* control) {
    SDL_Rect rect;

    for (int r = 0; r < MENU_ROW_COUNT; r++) {
        for (int c = MENU_CTRL_DECREMENT; c <= MENU_CTRL_ALL; c++) {
            if (rectangle_controle(r, c, &rect) && point_dans_rectangle(mx, my, rect.x, rect.y, rect.w, rect.h)) {
                *row = r;
                *control = c;
                return 1;
            }
        }
    }
    return 0;
}

void interface_dessiner_menu(UIContext* ctx) {
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    remplir(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, PAL_BACKGROUND, 255);
    degrade_chaleur(ctx, 0, 0, SCREEN_WIDTH, 4);

    texte(ctx, "Chess Engine", 84, 44, FONT_TITLE, PAL_FOREGROUND);
    texte(ctx, "Pick a mode on the left, tune each engine on the right.", 88, 120, FONT_TINY, PAL_MUTED);

    carte(ctx, 84, 190, 478, 492, 12, PAL_SURFACE, PAL_BORDER);
    texte(ctx, "Game modes", 112, 208, FONT_MEDIUM, PAL_FOREGROUND);
    texte(ctx, "CLICK A CARD TO START", 112, 238, FONT_TINY, PAL_MUTED);

    const char* options[] = {
        "PC vs PC",
        "Human vs PC",
        "Human vs PC",
        "Quit"
    };
    const char* subtitles[] = {
        "Watch both sides search and play",
        "You play Black, the engine plays White",
        "You play White, the engine plays Black",
        "Close the SDL client"
    };

    for (int i = 0; i < 4; i++) {
        int card_y = MENU_CARD_Y + i * MENU_CARD_STEP;
        interface_dessiner_carte_mode(ctx, i, MENU_CARD_X, card_y, MENU_CARD_W, MENU_CARD_H, options[i],
            subtitles[i], (int)ctx->selected_option == i, interface_menu_carte(mouse_x, mouse_y) == i);
    }

    // we explain the four search algorithms under the mode cards
    remplir(ctx, MENU_CARD_X, 592, MENU_CARD_W, 1, PAL_BORDER, 255);
    dessiner_raccourci(ctx, "SEQUENTIAL", "one core, root moves in order", MENU_CARD_X, 602);
    dessiner_raccourci(ctx, "ROOT SPLIT", "root moves shared out across threads", MENU_CARD_X, 622);
    dessiner_raccourci(ctx, "YBW TREE", "split at every deep node (OpenMP tasks)", MENU_CARD_X, 642);
    dessiner_raccourci(ctx, "LAZY SMP", "helpers fill the shared table ahead", MENU_CARD_X, 662);

    carte(ctx, 590, 190, 446, 492, 12, PAL_SURFACE, PAL_BORDER);
    texte(ctx, "Engine setup", 618, 208, FONT_MEDIUM, PAL_FOREGROUND);
    texte(ctx, "MOUSE OR KEYBOARD", 618, 238, FONT_TINY, PAL_MUTED);

    const char* row_labels[MENU_ROW_COUNT] = {
        "WHITE HEURISTIC", "WHITE SEARCH", "BLACK HEURISTIC", "BLACK SEARCH",
        "SEARCH DEPTH", "BEAM WIDTH", "BENCHMARK"
    };
    const Uint32 row_colors[MENU_ROW_COUNT] = {
        PAL_POSITIVE, PAL_POSITIVE, PAL_NEGATIVE, PAL_NEGATIVE, PAL_ACCENT, PAL_SERIES_2, PAL_FOREGROUND
    };
    char values[MENU_ROW_COUNT][48];
    int toggles[MENU_ROW_COUNT] = {0};

    snprintf(values[MENU_ROW_WHITE_EVAL], sizeof(values[0]), "%s", libelle_heuristique(ctx->est_white));
    snprintf(values[MENU_ROW_BLACK_EVAL], sizeof(values[0]), "%s", libelle_heuristique(ctx->est_black));
    snprintf(values[MENU_ROW_DEPTH], sizeof(values[0]), "%d plies", ctx->depth);
    if (ctx->width == 0) snprintf(values[MENU_ROW_WIDTH], sizeof(values[0]), "All moves");
    else snprintf(values[MENU_ROW_WIDTH], sizeof(values[0]), "%d moves", ctx->width);

    toggles[MENU_ROW_BENCHMARK] = ctx->benchmark;
    for (int r = MENU_ROW_WHITE_SEARCH; r <= MENU_ROW_BLACK_SEARCH; r += 2) {
        TypeRecherche algo = (r == MENU_ROW_WHITE_SEARCH) ? ctx->algo_white : ctx->algo_black;
        if (algo == RECHERCHE_SEQUENTIELLE) snprintf(values[r], sizeof(values[0]), "%s", algo_names[algo]);
        else snprintf(values[r], sizeof(values[0]), "%s \xC2\xB7 %d threads", algo_names[algo], ctx->omp_threads);
    }
    snprintf(values[MENU_ROW_BENCHMARK], sizeof(values[0]), "%s",
        ctx->benchmark ? "Vs sequential" : "Off");

    for (int r = 0; r < MENU_ROW_COUNT; r++) {
        int row_y = MENU_ROW_Y + r * MENU_ROW_STEP;
        SDL_Rect rect;

        rectangle_arrondi(ctx, MENU_ROW_X, row_y, MENU_ROW_W, MENU_ROW_H, 8, PAL_HEAT_0, 255);
        remplir(ctx, MENU_ROW_X, row_y + 12, 3, MENU_ROW_H - 24, row_colors[r], 255);
        texte(ctx, row_labels[r], MENU_ROW_X + 20, row_y + 6, FONT_TINY, PAL_MUTED);
        texte(ctx, values[r], MENU_ROW_X + 20, row_y + 21, FONT_MEDIUM, PAL_FOREGROUND);

        if (ligne_bascule(r)) {
            const char* left_label = "OFF";
            const char* right_label = "ON";

            rectangle_controle(r, MENU_CTRL_DECREMENT, &rect);
            interface_dessiner_controle_menu(ctx, rect.x, rect.y, rect.w, rect.h, left_label,
                point_dans_rectangle(mouse_x, mouse_y, rect.x, rect.y, rect.w, rect.h), !toggles[r]);
            rectangle_controle(r, MENU_CTRL_INCREMENT, &rect);
            interface_dessiner_controle_menu(ctx, rect.x, rect.y, rect.w, rect.h, right_label,
                point_dans_rectangle(mouse_x, mouse_y, rect.x, rect.y, rect.w, rect.h), toggles[r]);
            continue;
        }

        rectangle_controle(r, MENU_CTRL_DECREMENT, &rect);
        interface_dessiner_controle_menu(ctx, rect.x, rect.y, rect.w, rect.h, "-",
            point_dans_rectangle(mouse_x, mouse_y, rect.x, rect.y, rect.w, rect.h), 0);
        rectangle_controle(r, MENU_CTRL_INCREMENT, &rect);
        interface_dessiner_controle_menu(ctx, rect.x, rect.y, rect.w, rect.h, "+",
            point_dans_rectangle(mouse_x, mouse_y, rect.x, rect.y, rect.w, rect.h), 0);
        if (rectangle_controle(r, MENU_CTRL_ALL, &rect)) {
            interface_dessiner_controle_menu(ctx, rect.x, rect.y, rect.w, rect.h, "ALL",
                point_dans_rectangle(mouse_x, mouse_y, rect.x, rect.y, rect.w, rect.h), ctx->width == 0);
        }
    }

    carte(ctx, 84, 694, 952, 48, 12, PAL_SURFACE, PAL_BORDER);
    int hint_x = 112;
    int hint_y = 694 + (48 - (ctx->font_tiny ? TTF_FontHeight(ctx->font_tiny) : 12)) / 2;
    hint_x = dessiner_raccourci(ctx, "UP/DOWN", "mode", hint_x, hint_y);
    hint_x = dessiner_raccourci(ctx, "ENTER", "start", hint_x, hint_y);
    hint_x = dessiner_raccourci(ctx, "Q/A W/S", "heuristics", hint_x, hint_y);
    hint_x = dessiner_raccourci(ctx, "Z X", "cycle search", hint_x, hint_y);
    hint_x = dessiner_raccourci(ctx, "LEFT/RIGHT", "depth", hint_x, hint_y);
    hint_x = dessiner_raccourci(ctx, "E/D R", "width", hint_x, hint_y);
    dessiner_raccourci(ctx, "B", "benchmark", hint_x, hint_y);
}

static void voile(UIContext* ctx, Uint8 alpha) {
    remplir(ctx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, PAL_BACKGROUND, alpha);
}

void interface_dessiner_fin_partie(UIContext* ctx, int winner) {
    int box_w = 480;
    int box_h = 200;
    int box_x = SCREEN_WIDTH / 2 - box_w / 2;
    int box_y = SCREEN_HEIGHT / 2 - box_h / 2;
    const char* msg = winner == MAX ? "White wins" : (winner == MIN ? "Black wins" : "Draw");
    Uint32 msg_color = winner == MAX ? PAL_POSITIVE : (winner == MIN ? PAL_NEGATIVE : PAL_ACCENT);

    voile(ctx, 200);
    carte(ctx, box_x, box_y, box_w, box_h, 14, PAL_SURFACE, PAL_BORDER);
    degrade_chaleur(ctx, box_x + 14, box_y + 1, box_w - 28, 4);

    texte_centre(ctx, "GAME OVER", SCREEN_WIDTH / 2, box_y + 30, FONT_TINY, PAL_MUTED);
    texte_centre(ctx, msg, SCREEN_WIDTH / 2, box_y + 52, FONT_TITLE, msg_color);
    texte_centre(ctx, "Press ENTER to return to the menu", SCREEN_WIDTH / 2, box_y + 148, FONT_SMALL, PAL_MUTED);
}

static void position_boite_promotion(int* box_x, int* box_y, int* first_tile_x, int* tile_y) {
    *box_x = SCREEN_WIDTH / 2 - PROMO_BOX_W / 2;
    *box_y = SCREEN_HEIGHT / 2 - PROMO_BOX_H / 2;
    *first_tile_x = *box_x + (PROMO_BOX_W - (4 * PROMO_TILE + 3 * PROMO_GAP)) / 2;
    *tile_y = *box_y + 88;
}

int interface_option_promotion(int mx, int my) {
    int box_x, box_y, first_tile_x, tile_y;
    position_boite_promotion(&box_x, &box_y, &first_tile_x, &tile_y);

    for (int i = 0; i < 4; i++) {
        if (point_dans_rectangle(mx, my, first_tile_x + i * (PROMO_TILE + PROMO_GAP), tile_y,
                PROMO_TILE, PROMO_TILE)) {
            return i;
        }
    }
    return -1;
}

void interface_dessiner_promotion(UIContext* ctx, int is_white) {
    int box_x, box_y, first_tile_x, tile_y;
    int mouse_x = 0;
    int mouse_y = 0;
    const int values[] = {'n', 't', 'f', 'c'};
    int hovered;

    SDL_GetMouseState(&mouse_x, &mouse_y);
    hovered = interface_option_promotion(mouse_x, mouse_y);
    position_boite_promotion(&box_x, &box_y, &first_tile_x, &tile_y);

    voile(ctx, 200);
    carte(ctx, box_x, box_y, PROMO_BOX_W, PROMO_BOX_H, 14, PAL_SURFACE, PAL_BORDER);
    degrade_chaleur(ctx, box_x + 14, box_y + 1, PROMO_BOX_W - 28, 4);

    texte_centre(ctx, "Promote pawn", SCREEN_WIDTH / 2, box_y + 22, FONT_MEDIUM, PAL_FOREGROUND);
    texte_centre(ctx, "CLICK A PIECE OR PRESS 1-4 / Q R B N", SCREEN_WIDTH / 2, box_y + 54, FONT_TINY, PAL_MUTED);

    for (int i = 0; i < 4; i++) {
        int tile_x = first_tile_x + i * (PROMO_TILE + PROMO_GAP);
        int selected = (is_white && ctx->promotion_choice == values[i]) ||
                       (!is_white && ctx->promotion_choice == -values[i]);
        Uint32 border = (hovered == i) ? PAL_ACCENT : (selected ? PAL_HEAT_2 : PAL_BORDER);
        char key[2] = {(char)('1' + i), '\0'};

        carte(ctx, tile_x, tile_y, PROMO_TILE, PROMO_TILE, 10, (hovered == i) ? PAL_HEAT_1 : PAL_HEAT_0, border);
        dessiner_texture_piece(ctx, is_white ? values[i] : -values[i], tile_x + 8, tile_y + 6, PROMO_TILE - 16, 255);
        texte_centre(ctx, key, tile_x + PROMO_TILE / 2, tile_y + PROMO_TILE + 6, FONT_TINY,
            (hovered == i || selected) ? PAL_ACCENT : PAL_MUTED);
    }
}
