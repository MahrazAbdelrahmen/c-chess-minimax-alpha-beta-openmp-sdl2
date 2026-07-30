#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "jeu.h"
#include "ui.h"

struct config Partie[MAXPARTIE];
int PartieMode[MAXPARTIE];
int PartieLen = 0;
FILE *f;
int num_coup = 0;
int h0 = 0;
int (*Est[10])(struct config *);
int nbEst;
int nbAlpha = 0;
int nbBeta = 0;

void initialiser_variables_globales() {
    Est[0] = estimation_1;
    Est[1] = estimation_2;
    Est[2] = estimation_3;
    Est[3] = estimation_4;
    Est[4] = estimation_5;
    Est[5] = estimation_6;
    Est[6] = estimation_7;
    nbEst = 7;
    srand((unsigned int)time(NULL));
}

static void effacer_coups_valides(int valid_moves[8][8]) {
    memset(valid_moves, 0, sizeof(int) * 8 * 8);
}

static void enregistrer_position(struct config *conf, int side_to_move) {
    if (PartieLen == MAXPARTIE) {
        memmove(&Partie[0], &Partie[1], sizeof(Partie[0]) * (MAXPARTIE - 1));
        memmove(&PartieMode[0], &PartieMode[1], sizeof(PartieMode[0]) * (MAXPARTIE - 1));
        PartieLen--;
    }

    copier(conf, &Partie[PartieLen]);
    PartieMode[PartieLen] = side_to_move;
    PartieLen++;
}

static void ajouter_historique_coup(char historique[][20], int *nb_coups_ptr, const char *coup) {
    int index = *nb_coups_ptr;

    if (index >= MAXPARTIE) {
        memmove(historique, historique + 1, sizeof(historique[0]) * (MAXPARTIE - 1));
        index = MAXPARTIE - 1;
        *nb_coups_ptr = index;
    }

    snprintf(historique[index], 20, "%s", coup);
    (*nb_coups_ptr)++;
}

static int trouver_destination_depuis_source(struct config *before, struct config *after,
                                             int sx, int sy, int *dx, int *dy) {
    int moving_piece = before->mat[sy][sx];

    *dx = -1;
    *dy = -1;

    if (moving_piece == 0 || after->mat[sy][sx] != 0) {
        return 0;
    }

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if ((x == sx && y == sy) || before->mat[y][x] == after->mat[y][x]) continue;

            if (after->mat[y][x] == moving_piece) {
                *dx = x;
                *dy = y;
                return 1;
            }
        }
    }

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if ((x == sx && y == sy) || before->mat[y][x] == after->mat[y][x]) continue;

            if (after->mat[y][x] != 0 &&
                ((after->mat[y][x] > 0) == (moving_piece > 0))) {
                *dx = x;
                *dy = y;
                return 1;
            }
        }
    }

    return 0;
}

static void detecter_delta_coup(struct config *before, struct config *after,
                              int *sx, int *sy, int *dx, int *dy) {
    int fallback_sx = -1;
    int fallback_sy = -1;

    *sx = -1;
    *sy = -1;
    *dx = -1;
    *dy = -1;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (before->mat[y][x] == after->mat[y][x]) continue;

            if (before->mat[y][x] != 0 && after->mat[y][x] == 0) {
                if (abs((int)before->mat[y][x]) == 'r') {
                    *sx = x;
                    *sy = y;
                } else if (fallback_sx < 0) {
                    fallback_sx = x;
                    fallback_sy = y;
                }
            }
        }
    }

    if (*sx < 0) {
        *sx = fallback_sx;
        *sy = fallback_sy;
    }

    if (*sx >= 0) {
        trouver_destination_depuis_source(before, after, *sx, *sy, dx, dy);
    }
}

static int collecter_coups_correspondants(struct config *before, struct config *moves, int n,
                                  int sx, int sy, int dx, int dy,
                                  int *matches, int max_matches) {
    int count = 0;

    for (int i = 0; i < n; i++) {
        int move_dx = -1;
        int move_dy = -1;

        if (!trouver_destination_depuis_source(before, &moves[i], sx, sy, &move_dx, &move_dy)) {
            continue;
        }

        if (move_dx == dx && move_dy == dy) {
            if (count < max_matches) {
                matches[count] = i;
            }
            count++;
        }
    }

    return count;
}

static int choisir_coup_promotion(struct config *moves, const int *matches, int match_count,
                                 int dx, int dy, char promoted_piece) {
    for (int i = 0; i < match_count; i++) {
        int move_index = matches[i];

        if (moves[move_index].mat[dy][dx] == promoted_piece) {
            return move_index;
        }
    }

    return (match_count > 0) ? matches[0] : -1;
}

static int resoudre_etat_partie(struct config *conf, int side_to_move, int *winner) {
    int cout = 0;

    if (deja_visitee(conf, side_to_move) >= 3) {
        *winner = 0;
        return 1;
    }

    if (est_feuille(conf, side_to_move, &cout)) {
        if (cout > 0) *winner = MAX;
        else if (cout < 0) *winner = MIN;
        else *winner = 0;
        return 1;
    }

    return 0;
}

static void annoncer_resultat(int winner) {
    if (winner == MAX) {
        printf("Game Over! Winner: B\n");
        if (f) fprintf(f, "Victory: White\n");
    } else if (winner == MIN) {
        printf("Game Over! Winner: N\n");
        if (f) fprintf(f, "Victory: Black\n");
    } else {
        printf("Game Over! Draw\n");
        if (f) fprintf(f, "Draw\n");
    }
}

static void appliquer_coup_fils(struct config *conf, struct config *next_conf, char *coup,
                             char historique[][20], int *nb_coups_ptr,
                             UIContext *ui_ctx, int src_x, int src_y, int dst_x, int dst_y,
                             int *selected_x, int *selected_y, int valid_moves[8][8]) {
    formuler_coup(conf, next_conf, coup);
    copier(next_conf, conf);
    ajouter_historique_coup(historique, nb_coups_ptr, coup);
    num_coup++;

    ui_ctx->last_move_sx = src_x;
    ui_ctx->last_move_sy = src_y;
    ui_ctx->last_move_dx = dst_x;
    ui_ctx->last_move_dy = dst_y;

    *selected_x = -1;
    *selected_y = -1;
    effacer_coups_valides(valid_moves);
}

static void terminer_tour(struct config *conf, int *player_ptr, int *game_over_ptr,
                        int *winner_ptr, UIContext *ui_ctx) {
    int next_player = (*player_ptr == MAX) ? MIN : MAX;

    enregistrer_position(conf, next_player);

    if (resoudre_etat_partie(conf, next_player, winner_ptr)) {
        *game_over_ptr = 1;
        ui_ctx->state = STATE_GAME_OVER;
        annoncer_resultat(*winner_ptr);
    } else {
        *player_ptr = next_player;
        ui_ctx->state = STATE_PLAYING;
    }
}

static void demarrer_partie_sdl(UIContext *ui_ctx, struct config *conf, int *num_coup_ptr,
                           int *nb_coups_ptr, int *player_ptr, int *game_over_ptr,
                           int *winner_ptr, int *selected_x, int *selected_y,
                           int valid_moves[8][8]) {
    ui_ctx->game_mode = ui_ctx->selected_option + 1;
    ui_ctx->state = STATE_PLAYING;
    initialiser_configuration(conf);
    *num_coup_ptr = 0;
    *nb_coups_ptr = 0;
    *player_ptr = MAX;
    *game_over_ptr = 0;
    *winner_ptr = 0;
    *selected_x = -1;
    *selected_y = -1;
    ui_ctx->last_move_sx = -1;
    ui_ctx->last_move_sy = -1;
    ui_ctx->last_move_dx = -1;
    ui_ctx->last_move_dy = -1;
    effacer_coups_valides(valid_moves);
    PartieLen = 0;
    enregistrer_position(conf, MAX);
    printf("Game started. Mode: %d\n", ui_ctx->game_mode);
}

static int choisir_meilleur_coup_racine(struct config *conf, struct config *T, int n,
                                 int player_color, int depth, int width, int est_func,
                                 int *best_index, int *best_score) {
    int is_white = (player_color == MAX);
    int search_width = (width <= 0) ? INFINI : width;
    int nbp = nombre_pieces(conf);

    if (n <= 0) return 0;

    for (int i = 0; i < n; i++) T[i].val = Est[est_func](&T[i]);
    if (is_white) qsort(T, n, sizeof(struct config), comparer_config_321);
    else qsort(T, n, sizeof(struct config), comparer_config_123);

    if (search_width < n) n = search_width;

    *best_index = 0;
    *best_score = minmax_alpha_beta(&T[0], is_white ? MIN : MAX, depth, -INFINI, +INFINI,
                            search_width, est_func, nbp);

    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 1; i < n; i++) {
        int current_bound = *best_score;
        int value;

        if (is_white) {
            value = minmax_alpha_beta(&T[i], MIN, depth, current_bound, +INFINI,
                              search_width, est_func, nbp);
        } else {
            value = minmax_alpha_beta(&T[i], MAX, depth, -INFINI, current_bound,
                              search_width, est_func, nbp);
        }

        #pragma omp critical
        {
            if (is_white) {
                if (value > *best_score) {
                    *best_score = value;
                    *best_index = i;
                }
            } else {
                if (value < *best_score) {
                    *best_score = value;
                    *best_index = i;
                }
            }
        }
    }

    return 1;
}

int gerer_tour_pc(struct config *conf, struct config *T, char *coup_str, 
                   int player_color, int depth, int width, int est_func) {
    int n, j, score;
    
    generer_successeurs(conf, player_color, T, &n);
    
    if (n == 0) return 0;

    if (!choisir_meilleur_coup_racine(conf, T, n, player_color, depth, width, est_func, &j, &score)) {
        return 0;
    }
    
    if (j != -1) {
        formuler_coup(conf, &T[j], coup_str);
        copier(&T[j], conf);
        conf->val = score;
        return 1;
    }
    
    if (n > 0) {
        formuler_coup(conf, &T[0], coup_str);
        copier(&T[0], conf);
        return 1;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    UIContext ui_ctx;
    struct config T[MAX_MOVES], conf;
    char historique[MAXPARTIE][20];
    char coup[20] = "";
    char nomf[20] = "partie.txt";
    int nb_coups = 0;
    int player = MAX;
    int game_over = 0;
    int winner = 0;

    (void)argc;
    (void)argv;
    
    if (!interface_initialiser(&ui_ctx)) {
        printf("Failed to initialize UI\n");
        return 1;
    }

    initialiser_variables_globales();
    initialiser_zobrist();
    initialiser_configuration(&conf);
    ui_ctx.omp_threads = omp_get_max_threads();
    ui_ctx.omp_procs = omp_get_num_procs();
    printf("OpenMP threads: %d (processors: %d)\n", ui_ctx.omp_threads, ui_ctx.omp_procs);
    
    num_coup = 0;
    PartieLen = 0;
    enregistrer_position(&conf, MAX);
    
    f = fopen(nomf, "w");
    fprintf(f, "--- Chess Game - SDL2 UI ---\n");

    interface_charger_pieces(&ui_ctx);

    SDL_Event e;
    int running = 1;
    int selected_x = -1, selected_y = -1;
    int valid_moves[8][8] = {0};
    int n = 0;
    int pc_move_timer = 0;
    int promotion_move_indices[8] = {0};
    int promotion_move_count = 0;
    int promotion_src_x = -1, promotion_src_y = -1;
    int promotion_dst_x = -1, promotion_dst_y = -1;
    
    printf("Game initialized. Board has %d pieces.\n", nombre_pieces(&conf));
    
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            }
            
            if (ui_ctx.state == STATE_MENU) {
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_UP) {
                        ui_ctx.selected_option = (ui_ctx.selected_option + 3) % 4;
                    } else if (e.key.keysym.sym == SDLK_DOWN) {
                        ui_ctx.selected_option = (ui_ctx.selected_option + 1) % 4;
                    } else if (e.key.keysym.sym == SDLK_LEFT) {
                        if (ui_ctx.depth > 1) ui_ctx.depth--;
                    } else if (e.key.keysym.sym == SDLK_RIGHT) {
                        if (ui_ctx.depth < 8) ui_ctx.depth++;
                    } else if (e.key.keysym.sym == SDLK_q) {
                        if (ui_ctx.est_white > 0) ui_ctx.est_white--;
                    } else if (e.key.keysym.sym == SDLK_a) {
                        if (ui_ctx.est_white < nbEst - 1) ui_ctx.est_white++;
                    } else if (e.key.keysym.sym == SDLK_w) {
                        if (ui_ctx.est_black > 0) ui_ctx.est_black--;
                    } else if (e.key.keysym.sym == SDLK_s) {
                        if (ui_ctx.est_black < nbEst - 1) ui_ctx.est_black++;
                    } else if (e.key.keysym.sym == SDLK_e) {
                        if (ui_ctx.width == 0) ui_ctx.width = 4;
                        else if (ui_ctx.width > 1) ui_ctx.width--;
                    } else if (e.key.keysym.sym == SDLK_d) {
                        if (ui_ctx.width == 0) ui_ctx.width = 4;
                        else if (ui_ctx.width < 20) ui_ctx.width++;
                    } else if (e.key.keysym.sym == SDLK_r) {
                        ui_ctx.width = 0;
                    } else if (e.key.keysym.sym == SDLK_RETURN) {
                        if (ui_ctx.selected_option == 3) {
                            running = 0;
                        } else {
                            demarrer_partie_sdl(&ui_ctx, &conf, &num_coup, &nb_coups, &player,
                                           &game_over, &winner, &selected_x, &selected_y,
                                           valid_moves);
                        }
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    int option_x = 112;
                    int option_w = 422;
                    int option_h = 66;

                    for (int i = 0; i < 4; i++) {
                        int option_y = 298 + i * 82;
                        if (mx >= option_x && mx < option_x + option_w &&
                            my >= option_y && my < option_y + option_h) {
                            ui_ctx.selected_option = (MenuOption)i;
                            if (i == MENU_QUIT) {
                                running = 0;
                            } else {
                                demarrer_partie_sdl(&ui_ctx, &conf, &num_coup, &nb_coups, &player,
                                               &game_over, &winner, &selected_x, &selected_y,
                                               valid_moves);
                            }
                            break;
                        }
                    }

                    if (ui_ctx.state == STATE_MENU) {
                        if (mx >= 930 && mx < 962) {
                            if (my >= 314 && my < 346 && ui_ctx.est_white > 0) ui_ctx.est_white--;
                            else if (my >= 396 && my < 428 && ui_ctx.est_black > 0) ui_ctx.est_black--;
                            else if (my >= 478 && my < 510 && ui_ctx.depth > 1) ui_ctx.depth--;
                            else if (my >= 560 && my < 592) {
                                if (ui_ctx.width == 0) ui_ctx.width = 4;
                                else if (ui_ctx.width > 1) ui_ctx.width--;
                            }
                        } else if (mx >= 968 && mx < 1000) {
                            if (my >= 314 && my < 346 && ui_ctx.est_white < nbEst - 1) ui_ctx.est_white++;
                            else if (my >= 396 && my < 428 && ui_ctx.est_black < nbEst - 1) ui_ctx.est_black++;
                            else if (my >= 478 && my < 510 && ui_ctx.depth < 8) ui_ctx.depth++;
                            else if (my >= 560 && my < 592) {
                                if (ui_ctx.width == 0) ui_ctx.width = 4;
                                else if (ui_ctx.width < 20) ui_ctx.width++;
                            }
                        } else if (mx >= 894 && mx < 990 && my >= 560 && my < 592) {
                            ui_ctx.width = 0;
                        }
                    }
                }
            }
            else if (ui_ctx.state == STATE_PROMOTION) {
                char choice = 0;

                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_1:
                        case SDLK_q:
                            choice = 'n';
                            break;
                        case SDLK_2:
                        case SDLK_r:
                            choice = 't';
                            break;
                        case SDLK_3:
                        case SDLK_b:
                            choice = 'f';
                            break;
                        case SDLK_4:
                        case SDLK_n:
                            choice = 'c';
                            break;
                        case SDLK_ESCAPE:
                            ui_ctx.state = STATE_PLAYING;
                            break;
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int box_w = 320, box_h = 100;
                    int box_x = SCREEN_WIDTH / 2 - box_w / 2;
                    int box_y = SCREEN_HEIGHT / 2 - box_h / 2;
                    int option_values[] = {'n', 't', 'f', 'c'};

                    for (int i = 0; i < 4; i++) {
                        int opt_x = box_x + 16 + i * 72;
                        int opt_y = box_y + 46;
                        if (e.button.x >= opt_x && e.button.x < opt_x + 64 &&
                            e.button.y >= opt_y && e.button.y < opt_y + 40) {
                            choice = (char)option_values[i];
                            break;
                        }
                    }
                }

                if (choice != 0) {
                    int selected_move = -1;
                    char signed_choice = (player == MAX) ? choice : (char)-choice;

                    ui_ctx.promotion_choice = signed_choice;
                    selected_move = choisir_coup_promotion(T, promotion_move_indices, promotion_move_count,
                                                          promotion_dst_x, promotion_dst_y, signed_choice);

                    if (selected_move >= 0) {
                        appliquer_coup_fils(&conf, &T[selected_move], coup, historique, &nb_coups,
                                         &ui_ctx, promotion_src_x, promotion_src_y,
                                         promotion_dst_x, promotion_dst_y,
                                         &selected_x, &selected_y, valid_moves);
                        terminer_tour(&conf, &player, &game_over, &winner, &ui_ctx);
                    }
                }
            }
            else if (ui_ctx.state == STATE_PLAYING && !game_over) {
                int is_white = (player == MAX);
                int human_turn = (ui_ctx.game_mode == 2 && !is_white) || 
                                (ui_ctx.game_mode == 3 && is_white);
                
                if (human_turn && e.type == SDL_MOUSEBUTTONDOWN) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    
                    if (mx >= BOARD_OFFSET_X && mx < BOARD_OFFSET_X + TILE_SIZE * 8 &&
                        my >= BOARD_OFFSET_Y && my < BOARD_OFFSET_Y + TILE_SIZE * 8) {
                        
                        int gx = (mx - BOARD_OFFSET_X) / TILE_SIZE;
                        int gy = (my - BOARD_OFFSET_Y) / TILE_SIZE;
                        
                        // we index gx/gy 0-7 (file a-h, rank 1-8), matching mat[row][col] where row 0 is rank 1
                        if (e.button.button == SDL_BUTTON_RIGHT) {
                            selected_x = -1;
                            selected_y = -1;
                            effacer_coups_valides(valid_moves);
                        } else if (e.button.button == SDL_BUTTON_LEFT) {
                            if (selected_x >= 0 && valid_moves[gy][gx]) {
                                generer_successeurs(&conf, player, T, &n);

                                int move_indices[8] = {0};
                                int match_count = collecter_coups_correspondants(&conf, T, n, selected_x, selected_y,
                                                                        gx, gy, move_indices, 8);

                                if (match_count == 1) {
                                    appliquer_coup_fils(&conf, &T[move_indices[0]], coup, historique, &nb_coups,
                                                     &ui_ctx, selected_x, selected_y, gx, gy,
                                                     &selected_x, &selected_y, valid_moves);
                                    printf("Move %d: %s (player=%c)\n", num_coup, coup, player == MAX ? 'B' : 'N');
                                    terminer_tour(&conf, &player, &game_over, &winner, &ui_ctx);
                                } else if (match_count > 1) {
                                    promotion_move_count = (match_count < 8) ? match_count : 8;
                                    memcpy(promotion_move_indices, move_indices, sizeof(move_indices));
                                    promotion_src_x = selected_x;
                                    promotion_src_y = selected_y;
                                    promotion_dst_x = gx;
                                    promotion_dst_y = gy;
                                    ui_ctx.promotion_choice = (player == MAX) ? 'n' : (char)-'n';
                                    ui_ctx.promotion_row = gy;
                                    ui_ctx.promotion_col = gx;
                                    ui_ctx.state = STATE_PROMOTION;
                                } else {
                                    printf("Move not found in generated moves!\n");
                                }
                            } else {
                                if (conf.mat[gy][gx] != 0) {
                                    int piece = conf.mat[gy][gx];
                                    if ((is_white && piece > 0) || (!is_white && piece < 0)) {
                                        selected_x = gx;
                                        selected_y = gy;

                                        generer_successeurs(&conf, player, T, &n);
                                        effacer_coups_valides(valid_moves);

                                        printf("Selected piece at (%d,%d), generated %d moves\n",
                                            selected_x, selected_y, n);

                                        for (int i = 0; i < n; i++) {
                                            int move_dx = -1;
                                            int move_dy = -1;

                                            if (trouver_destination_depuis_source(&conf, &T[i],
                                                                                  selected_x, selected_y,
                                                                                  &move_dx, &move_dy)) {
                                                valid_moves[move_dy][move_dx] = 1;
                                            }
                                        }
                                        
                                        // we log the count here for debugging move generation
                                        int valid_count = 0;
                                        for (int by = 0; by < 8; by++) {
                                            for (int bx = 0; bx < 8; bx++) {
                                                if (valid_moves[by][bx]) valid_count++;
                                            }
                                        }
                                        printf("  Valid moves: %d\n", valid_count);
                                    }
                                }
                            }
                        }
                    }
                }
                
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        ui_ctx.state = STATE_MENU;
                    }
                }
            }
            else if (ui_ctx.state == STATE_GAME_OVER) {
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN) {
                    ui_ctx.state = STATE_MENU;
                }
            }
        }
        
        if (!game_over && ui_ctx.state == STATE_PLAYING) {
            int is_white = (player == MAX);
            int pc_turn = (ui_ctx.game_mode == 1) || 
                        (ui_ctx.game_mode == 2 && is_white) ||
                        (ui_ctx.game_mode == 3 && !is_white);
            
            if (pc_turn) {
                pc_move_timer++;
                if (pc_move_timer > 15) {
                    int est_func = is_white ? ui_ctx.est_white : ui_ctx.est_black;
                    generer_successeurs(&conf, player, T, &n);
                    
                    if (n == 0) {
                        if (resoudre_etat_partie(&conf, player, &winner)) {
                            game_over = 1;
                            ui_ctx.state = STATE_GAME_OVER;
                            annoncer_resultat(winner);
                        }
                    } else {
                    struct config before_move;
                    copier(&conf, &before_move);

                    int success = gerer_tour_pc(&conf, T, coup, player,
                                                    ui_ctx.depth, ui_ctx.width, est_func);
                        
                        if (!success) {
                            if (resoudre_etat_partie(&conf, player, &winner)) {
                                game_over = 1;
                                ui_ctx.state = STATE_GAME_OVER;
                                annoncer_resultat(winner);
                            }
                        } else {
                            ajouter_historique_coup(historique, &nb_coups, coup);
                            num_coup++;

                            detecter_delta_coup(&before_move, &conf,
                                &ui_ctx.last_move_sx, &ui_ctx.last_move_sy,
                                &ui_ctx.last_move_dx, &ui_ctx.last_move_dy);
                            
                            printf("PC Move %d: %s (player=%c)\n", num_coup, coup, player == MAX ? 'B' : 'N');
                            terminer_tour(&conf, &player, &game_over, &winner, &ui_ctx);
                        }
                    }
                    pc_move_timer = 0;
                }
            }
        }
        
        SDL_SetRenderDrawColor(ui_ctx.renderer, 20, 20, 30, 255);
        SDL_RenderClear(ui_ctx.renderer);
        
        if (ui_ctx.state == STATE_MENU) {
            interface_dessiner_menu(&ui_ctx);
        } else if (ui_ctx.state == STATE_PLAYING || ui_ctx.state == STATE_GAME_OVER ||
                   ui_ctx.state == STATE_PROMOTION) {
            memcpy(ui_ctx.valid_moves, valid_moves, sizeof(valid_moves));
            ui_ctx.selected_x = selected_x;
            ui_ctx.selected_y = selected_y;
            
            interface_dessiner_plateau(&ui_ctx, &conf);
            interface_dessiner_pieces(&ui_ctx, &conf);
            interface_dessiner_panneau(&ui_ctx, &conf, num_coup, coup, player);
            
            if (game_over) {
                interface_dessiner_fin_partie(&ui_ctx, winner);
            } else if (ui_ctx.state == STATE_PROMOTION) {
                interface_dessiner_promotion(&ui_ctx, player == MAX);
            }
        }
        
        SDL_RenderPresent(ui_ctx.renderer);
        SDL_Delay(16);
    }
    
    fprintf(f, "Game ended. Total moves: %d\n", num_coup);
    fclose(f);
    interface_nettoyer(&ui_ctx);
    
    return 0;
}
