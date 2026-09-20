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
    ui_ctx->thinking = 0;
    ui_ctx->thinking_seconds = 0.0;
    memset(ui_ctx->last_search, 0, sizeof(ui_ctx->last_search));
    memset(&ui_ctx->bench, 0, sizeof(ui_ctx->bench));
    printf("Game started. Mode: %d\n", ui_ctx->game_mode);
}

static int borner(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void appliquer_reglage_menu(UIContext *ui_ctx, int row, int control) {
    int step = (control == MENU_CTRL_INCREMENT) ? 1 : -1;

    switch (row) {
        case MENU_ROW_WHITE_EVAL:
            ui_ctx->est_white = borner(ui_ctx->est_white + step, 0, nbEst - 1);
            break;
        case MENU_ROW_BLACK_EVAL:
            ui_ctx->est_black = borner(ui_ctx->est_black + step, 0, nbEst - 1);
            break;
        case MENU_ROW_WHITE_SEARCH:
            ui_ctx->algo_white = (TypeRecherche)((ui_ctx->algo_white + step + RECHERCHE_COUNT) % RECHERCHE_COUNT);
            break;
        case MENU_ROW_BLACK_SEARCH:
            ui_ctx->algo_black = (TypeRecherche)((ui_ctx->algo_black + step + RECHERCHE_COUNT) % RECHERCHE_COUNT);
            break;
        case MENU_ROW_DEPTH:
            ui_ctx->depth = borner(ui_ctx->depth + step, 1, 8);
            break;
        case MENU_ROW_WIDTH:
            if (control == MENU_CTRL_ALL) ui_ctx->width = 0;
            else if (ui_ctx->width == 0) ui_ctx->width = 4;
            else ui_ctx->width = borner(ui_ctx->width + step, 1, 20);
            break;
        case MENU_ROW_BENCHMARK:
            ui_ctx->benchmark = (control == MENU_CTRL_INCREMENT);
            break;
    }
}

static int basculer(int on) {
    return on ? MENU_CTRL_DECREMENT : MENU_CTRL_INCREMENT;
}

typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t done;
    Uint32 started;

    struct config conf;
    int player, depth, width, est_func, benchmark;
    TypeRecherche algo;

    int success;
    struct config chosen;
    ResultatRecherche res;

    int compared;
    struct config seq_move;
    ResultatRecherche seq;
} RecherchePC;

static int executer_recherche(void *data) {
    RecherchePC *job = data;
    struct config T[MAX_MOVES];
    int n = 0;

    job->success = chercher_meilleur_coup(&job->conf, job->player, job->depth, job->width, job->est_func,
                                          job->algo, T, &n, &job->res);
    if (job->success) {
        copier(&T[job->res.best_index], &job->chosen);

        if (job->benchmark && job->algo != RECHERCHE_SEQUENTIELLE &&
            chercher_meilleur_coup(&job->conf, job->player, job->depth, job->width, job->est_func,
                                   RECHERCHE_SEQUENTIELLE, T, &n, &job->seq)) {
            copier(&T[job->seq.best_index], &job->seq_move);
            job->compared = 1;
        }
    }

    SDL_AtomicSet(&job->done, 1);
    return 0;
}

static void lancer_recherche(RecherchePC *job, UIContext *ui_ctx, struct config *conf, int player) {
    int is_white = (player == MAX);

    memset(job, 0, sizeof(*job));
    copier(conf, &job->conf);
    job->player = player;
    job->depth = ui_ctx->depth;
    job->width = ui_ctx->width;
    job->est_func = is_white ? ui_ctx->est_white : ui_ctx->est_black;
    job->algo = is_white ? ui_ctx->algo_white : ui_ctx->algo_black;
    job->benchmark = ui_ctx->benchmark;
    job->started = SDL_GetTicks();
    job->thread = SDL_CreateThread(executer_recherche, "engine", job);
}

static void abandonner_recherche(RecherchePC *job) {
    if (!job->thread) return;
    annuler_recherche(1);
    SDL_WaitThread(job->thread, NULL);
    annuler_recherche(0);
    job->thread = NULL;
}

static const char *nom_fichier_benchmark = "benchmark.csv";
static const char *entete_benchmark =
    "move,side,algo,heuristic,depth,width,threads,seq_ms,algo_ms,speedup,"
    "seq_nodes,algo_nodes,seq_score,algo_score,seq_move,algo_move,same_move\n";

static void journaliser_benchmark(RecherchePC *job) {
    char first_line[256] = "";
    FILE *csv = fopen(nom_fichier_benchmark, "r");
    char seq_coup[20];
    char algo_coup[20];

    if (csv) {
        if (!fgets(first_line, sizeof(first_line), csv)) first_line[0] = '\0';
        fclose(csv);
        if (first_line[0] && strcmp(first_line, entete_benchmark) != 0) {
            remove("benchmark.old.csv");
            rename(nom_fichier_benchmark, "benchmark.old.csv");
            first_line[0] = '\0';
        }
    }

    csv = fopen(nom_fichier_benchmark, "a");
    if (!csv) return;
    if (!first_line[0]) fputs(entete_benchmark, csv);

    formuler_coup(&job->conf, &job->seq_move, seq_coup);
    formuler_coup(&job->conf, &job->chosen, algo_coup);
    fprintf(csv, "%d,%s,%s,%d,%d,%d,%d,%.3f,%.3f,%.3f,%lld,%lld,%d,%d,%s,%s,%d\n",
            num_coup + 1, job->player == MAX ? "white" : "black", interface_nom_algo(job->algo),
            job->est_func + 1, job->depth, job->width, job->res.threads,
            job->seq.seconds * 1000.0, job->res.seconds * 1000.0,
            job->res.seconds > 0.0 ? job->seq.seconds / job->res.seconds : 0.0,
            job->seq.nodes, job->res.nodes, job->seq.score, job->res.score, seq_coup, algo_coup,
            configurations_egales(job->seq_move.mat, job->chosen.mat));
    fclose(csv);
}

static void comptabiliser_recherche(UIContext *ui_ctx, RecherchePC *job) {
    DerniereRecherche *last = &ui_ctx->last_search[job->player == MAX ? 0 : 1];

    last->valid = 1;
    last->algo = job->algo;
    last->seconds = job->res.seconds;
    last->nodes = job->res.nodes;
    last->threads = job->res.threads;

    if (!job->compared) return;

    StatsBenchmark *bench = &ui_ctx->bench[job->algo];
    int same_move = configurations_egales(job->seq_move.mat, job->chosen.mat);

    bench->compared++;
    bench->same_move += same_move;
    bench->same_score += (job->seq.score == job->res.score);
    bench->seq_seconds += job->seq.seconds;
    bench->algo_seconds += job->res.seconds;
    bench->seq_nodes += job->seq.nodes;
    bench->algo_nodes += job->res.nodes;

    printf("  bench: seq %.3fs %lld nodes score %d | %s %.3fs %lld nodes score %d | speedup %.2fx | %s\n",
           job->seq.seconds, job->seq.nodes, job->seq.score, interface_nom_algo(job->algo),
           job->res.seconds, job->res.nodes, job->res.score,
           job->res.seconds > 0.0 ? job->seq.seconds / job->res.seconds : 0.0,
           same_move ? "same move" : "DIFFERENT move");
    journaliser_benchmark(job);
}

typedef struct {
    int autostart;  // start a PC vs PC game immediately
    int quit_after;
} OptionsLancement;

static TypeRecherche lire_algo(const char *value) {
    if (value && strcmp(value, "seq") == 0) return RECHERCHE_SEQUENTIELLE;
    if (value && strcmp(value, "par") == 0) return RECHERCHE_PARALLELE;
    if (value && strcmp(value, "lazy") == 0) return RECHERCHE_LAZY_SMP;
    return RECHERCHE_YBW;
}

static void lire_options(int argc, char *argv[], UIContext *ui_ctx, OptionsLancement *options) {
    memset(options, 0, sizeof(*options));

    for (int i = 1; i < argc; i++) {
        const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(argv[i], "--pcvpc") == 0) options->autostart = 1;
        else if (strcmp(argv[i], "--benchmark") == 0) ui_ctx->benchmark = 1;
        else if (strcmp(argv[i], "--depth") == 0 && next) { ui_ctx->depth = borner(atoi(next), 1, 8); i++; }
        else if (strcmp(argv[i], "--width") == 0 && next) { ui_ctx->width = borner(atoi(next), 0, 20); i++; }
        else if (strcmp(argv[i], "--heuristic") == 0 && next) {
            ui_ctx->est_white = ui_ctx->est_black = borner(atoi(next) - 1, 0, nbEst - 1);
            i++;
        }
        else if (strcmp(argv[i], "--white") == 0 && next) { ui_ctx->algo_white = lire_algo(next); i++; }
        else if (strcmp(argv[i], "--black") == 0 && next) { ui_ctx->algo_black = lire_algo(next); i++; }
        else if (strcmp(argv[i], "--quit-after") == 0 && next) { options->quit_after = atoi(next); i++; }
        else printf("Unknown option: %s\n", argv[i]);
    }
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

    OptionsLancement options;
    
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
    lire_options(argc, argv, &ui_ctx, &options);

    SDL_Event e;
    int running = 1;
    int selected_x = -1, selected_y = -1;
    int valid_moves[8][8] = {0};
    int n = 0;
    int pc_move_timer = 0;
    RecherchePC recherche;
    memset(&recherche, 0, sizeof(recherche));
    int promotion_move_indices[8] = {0};
    int promotion_move_count = 0;
    int promotion_src_x = -1, promotion_src_y = -1;
    int promotion_dst_x = -1, promotion_dst_y = -1;
    
    printf("Game initialized. Board has %d pieces.\n", nombre_pieces(&conf));
    
    if (options.autostart) {
        ui_ctx.selected_option = MENU_PVPC;
        demarrer_partie_sdl(&ui_ctx, &conf, &num_coup, &nb_coups, &player, &game_over, &winner,
                       &selected_x, &selected_y, valid_moves);
    }

    while (running) {
        if (options.autostart && (game_over || (options.quit_after > 0 && num_coup >= options.quit_after))) {
            running = 0;
        }
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            }
            
            if (ui_ctx.state == STATE_MENU) {
                int row = -1;
                int control = MENU_CTRL_DECREMENT;

                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_UP: ui_ctx.selected_option = (ui_ctx.selected_option + 3) % 4; break;
                        case SDLK_DOWN: ui_ctx.selected_option = (ui_ctx.selected_option + 1) % 4; break;
                        case SDLK_LEFT: row = MENU_ROW_DEPTH; control = MENU_CTRL_DECREMENT; break;
                        case SDLK_RIGHT: row = MENU_ROW_DEPTH; control = MENU_CTRL_INCREMENT; break;
                        case SDLK_q: row = MENU_ROW_WHITE_EVAL; control = MENU_CTRL_DECREMENT; break;
                        case SDLK_a: row = MENU_ROW_WHITE_EVAL; control = MENU_CTRL_INCREMENT; break;
                        case SDLK_w: row = MENU_ROW_BLACK_EVAL; control = MENU_CTRL_DECREMENT; break;
                        case SDLK_s: row = MENU_ROW_BLACK_EVAL; control = MENU_CTRL_INCREMENT; break;
                        case SDLK_e: row = MENU_ROW_WIDTH; control = MENU_CTRL_DECREMENT; break;
                        case SDLK_d: row = MENU_ROW_WIDTH; control = MENU_CTRL_INCREMENT; break;
                        case SDLK_r: row = MENU_ROW_WIDTH; control = MENU_CTRL_ALL; break;
                        case SDLK_z: row = MENU_ROW_WHITE_SEARCH; control = MENU_CTRL_INCREMENT; break;
                        case SDLK_x: row = MENU_ROW_BLACK_SEARCH; control = MENU_CTRL_INCREMENT; break;
                        case SDLK_b: row = MENU_ROW_BENCHMARK; control = basculer(ui_ctx.benchmark); break;
                        case SDLK_RETURN:
                            if (ui_ctx.selected_option == MENU_QUIT) {
                                running = 0;
                            } else {
                                demarrer_partie_sdl(&ui_ctx, &conf, &num_coup, &nb_coups, &player,
                                               &game_over, &winner, &selected_x, &selected_y,
                                               valid_moves);
                            }
                            break;
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int card = interface_menu_carte(e.button.x, e.button.y);

                    if (card >= 0) {
                        ui_ctx.selected_option = (MenuOption)card;
                        if (card == MENU_QUIT) {
                            running = 0;
                        } else {
                            demarrer_partie_sdl(&ui_ctx, &conf, &num_coup, &nb_coups, &player,
                                           &game_over, &winner, &selected_x, &selected_y,
                                           valid_moves);
                        }
                    } else {
                        interface_menu_controle(e.button.x, e.button.y, &row, &control);
                    }
                }

                if (row >= 0) appliquer_reglage_menu(&ui_ctx, row, control);
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
                    int option_values[] = {'n', 't', 'f', 'c'};
                    int option = interface_option_promotion(e.button.x, e.button.y);

                    if (option >= 0) choice = (char)option_values[option];
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
                        abandonner_recherche(&recherche);
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
            
            if (pc_turn && !recherche.thread) {
                pc_move_timer++;
                if (pc_move_timer > 15) {
                    lancer_recherche(&recherche, &ui_ctx, &conf, player);
                    pc_move_timer = 0;
                }
            } else if (pc_turn && SDL_AtomicGet(&recherche.done)) {
                SDL_WaitThread(recherche.thread, NULL);
                recherche.thread = NULL;

                if (!recherche.success) {
                    if (resoudre_etat_partie(&conf, player, &winner)) {
                        game_over = 1;
                        ui_ctx.state = STATE_GAME_OVER;
                        annoncer_resultat(winner);
                    }
                } else {
                    struct config before_move;
                    copier(&conf, &before_move);

                    comptabiliser_recherche(&ui_ctx, &recherche);
                    formuler_coup(&conf, &recherche.chosen, coup);
                    copier(&recherche.chosen, &conf);
                    conf.val = recherche.res.score;

                    ajouter_historique_coup(historique, &nb_coups, coup);
                    num_coup++;

                    detecter_delta_coup(&before_move, &conf,
                        &ui_ctx.last_move_sx, &ui_ctx.last_move_sy,
                        &ui_ctx.last_move_dx, &ui_ctx.last_move_dy);

                    printf("PC Move %d: %s (player=%c)\n", num_coup, coup, player == MAX ? 'B' : 'N');
                    terminer_tour(&conf, &player, &game_over, &winner, &ui_ctx);
                }
            }
        }

        if (recherche.thread && ui_ctx.state != STATE_PLAYING) abandonner_recherche(&recherche);

        ui_ctx.thinking = !game_over && ui_ctx.state == STATE_PLAYING &&
            ((ui_ctx.game_mode == 1) || (ui_ctx.game_mode == 2 && player == MAX) ||
             (ui_ctx.game_mode == 3 && player == MIN));
        ui_ctx.thinking_seconds = recherche.thread ? (SDL_GetTicks() - recherche.started) / 1000.0 : 0.0;

        interface_effacer_ecran(&ui_ctx);
        
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
    
    abandonner_recherche(&recherche);
    fprintf(f, "Game ended. Total moves: %d\n", num_coup);
    fclose(f);
    interface_nettoyer(&ui_ctx);
    
    return 0;
}
