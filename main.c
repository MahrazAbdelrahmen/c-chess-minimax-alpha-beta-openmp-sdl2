#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <limits.h>
#include "jeu.h"

// we track visited positions here to detect repetitions
struct config Partie[MAXPARTIE];
int PartieMode[MAXPARTIE];
int PartieLen = 0;

FILE *f;
int num_coup = 0;
int h0 = 0;

int (*Est[10])(struct config *);
int nbEst;

// alpha/beta cutoff counters, for stats
int nbAlpha = 0;
int nbBeta = 0;

#define COL_RESET   "\033[0m"
#define COL_RED     "\033[1;31m"
#define COL_GRN     "\033[1;32m"
#define COL_YEL     "\033[1;33m"
#define COL_BLU     "\033[1;34m"
#define COL_BLK     "\033[1;30m"
#define COL_MAG     "\033[1;35m"
#define COL_CYN     "\033[1;36m"
#define COL_WHT     "\033[1;37m"
#define COL_BG_WHT  "\033[47m"
#define COL_BG_BLK  "\033[40m"
#define COL_BG_GRY  "\033[100m"

#define LIGHT_SQ    "\033[48;5;230m"
#define DARK_SQ     "\033[48;5;94m"

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

static void afficher_resultat_et_journaliser(int winner) {
    if (winner == MAX) {
        printf(COL_GRN "\n *** Le joueur Blanc 'B' gagne ***\n" COL_RESET);
        fprintf(f, "Victory: White\n");
    } else if (winner == MIN) {
        printf(COL_GRN "\n *** Le joueur Noir 'N' gagne ***\n" COL_RESET);
        fprintf(f, "Victory: Black\n");
    } else {
        printf(COL_YEL "\n *** Match nul ***\n" COL_RESET);
        fprintf(f, "Draw\n");
    }
}

void afficher_piece_unicode(char piece) {
    switch(piece) {
        case 'r': printf("♔"); break;
        case 'n': printf("♘"); break;
        case 'f': printf("♗"); break;
        case 't': printf("♖"); break;
        case 'c': printf("♕"); break;
        case 'p': printf("♙"); break;
        case -'r': printf("♚"); break;
        case -'n': printf("♞"); break;
        case -'f': printf("♝"); break;
        case -'t': printf("♜"); break;
        case -'c': printf("♛"); break;
        case -'p': printf("♟"); break;
        default: printf(" "); break;
    }
}

// ascii fallback for terminals without unicode support
void afficher_piece_ascii(char piece) {
    switch(piece) {
        case 'r': printf(" K "); break;
        case 'n': printf(" N "); break;
        case 'f': printf(" B "); break;
        case 't': printf(" R "); break;
        case 'c': printf(" Q "); break;
        case 'p': printf(" P "); break;
        case -'r': printf(" k "); break;
        case -'n': printf(" n "); break;
        case -'f': printf(" b "); break;
        case -'t': printf(" r "); break;
        case -'c': printf(" q "); break;
        case -'p': printf(" p "); break;
        default: printf("   "); break;
    }
}

void afficher_echiquier_ameliore(struct config *conf, int use_unicode) {
    int i, j;
    
    printf("\n");
    printf("    ");
    for (j = 0; j < 8; j++) {
        printf(COL_CYN "  %c  " COL_RESET, 'a' + j);
    }
    printf("\n");
    
    printf("   ╔");
    for (j = 0; j < 8; j++) {
        printf("════%s", (j < 7) ? "╦" : "╗");
    }
    printf("\n");
    
    for (i = 7; i >= 0; i--) {
        printf(COL_CYN " %d " COL_RESET, i + 1);
        printf("║");
        
        for (j = 0; j < 8; j++) {
            if ((i + j) % 2 == 0) {
                printf(LIGHT_SQ);
            } else {
                printf(DARK_SQ);
            }

            char piece = conf->mat[i][j];
            if (use_unicode) {
                printf(" ");
                if (piece > 0) printf(COL_WHT);
                else printf(COL_BLK);
                afficher_piece_unicode(piece);
                printf(" ");
            } else {

                if (piece > 0) printf(COL_WHT);
                else printf(COL_BLK);
                afficher_piece_ascii(piece);
            }
            
            printf(COL_RESET);
            printf("║");
        }
        
        printf(COL_CYN " %d" COL_RESET, i + 1);
        printf("\n");
        
        if (i > 0) {
            printf("   ╠");
            for (j = 0; j < 8; j++) {
                printf("════%s", (j < 7) ? "╬" : "╣");
            }
            printf("\n");
        }
    }
    
    printf("   ╚");
    for (j = 0; j < 8; j++) {
        printf("════%s", (j < 7) ? "╩" : "╝");
    }
    printf("\n");
    
    printf("    ");
    for (j = 0; j < 8; j++) {
        printf(COL_CYN "  %c  " COL_RESET, 'a' + j);
    }
    printf("\n\n");
}

void afficher_panneau_infos(struct config *conf, char *dernier_coup, int num_coup, int player) {
    printf(COL_CYN "╔════════════════════════════════════════╗\n");
    printf("║" COL_WHT "          GAME INFORMATION              " COL_CYN "║\n");
    printf("╠════════════════════════════════════════╣\n" COL_RESET);
    
    printf(COL_CYN "║ " COL_RESET "Move Number: " COL_YEL "%3d                     " COL_CYN "║\n" COL_RESET, num_coup);
    
    printf(COL_CYN "║ " COL_RESET "Current Turn: ");
    if (player == MAX) printf(COL_WHT "White (B)              " COL_CYN "║\n" COL_RESET);
    else printf(COL_BLK "Black (N)              " COL_CYN "║\n" COL_RESET);
    
    if (strlen(dernier_coup) > 0) {
        printf(COL_CYN "║ " COL_RESET "Last Move: " COL_GRN "%-22s" COL_CYN "║\n" COL_RESET, dernier_coup);
    } else {
        printf(COL_CYN "║ " COL_RESET "Last Move: " COL_GRN "%-22s" COL_CYN "║\n" COL_RESET, "---");
    }
    
    int white_pieces = 0, black_pieces = 0;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            char p = conf->mat[i][j];
            if (p > 0) white_pieces++;
            else if (p < 0) black_pieces++;
        }
    }
    
    printf(COL_CYN "║ " COL_RESET "White Pieces: " COL_WHT "%2d                  " COL_CYN "║\n" COL_RESET, white_pieces);
    printf(COL_CYN "║ " COL_RESET "Black Pieces: " COL_BLK "%2d                  " COL_CYN "║\n" COL_RESET, black_pieces);
    
    if (conf->val != 0) {
        printf(COL_CYN "║ " COL_RESET "Evaluation: ");
        if (conf->val > 0) printf(COL_GRN "+%-3d                   " COL_CYN "║\n" COL_RESET, conf->val);
        else printf(COL_RED "%-4d                   " COL_CYN "║\n" COL_RESET, conf->val);
    }
    
    printf(COL_CYN "╚════════════════════════════════════════╝\n" COL_RESET);
}

void afficher_historique(char historique[][20], int nb_coups) {
    if (nb_coups == 0) return;
    
    printf(COL_CYN "\n╔════════════════════════════════════════╗\n");
    printf("║" COL_WHT "           MOVE HISTORY                 " COL_CYN "║\n");
    printf("╠════════════════════════════════════════╣\n" COL_RESET);
    
    int start = (nb_coups > 10) ? nb_coups - 10 : 0;
    
    for (int i = start; i < nb_coups; i++) {
        printf(COL_CYN "║ " COL_RESET);
        printf("%2d. ", i + 1);
        
        if (i % 2 == 0) {
            printf(COL_WHT "%-12s" COL_RESET, historique[i]);
        } else {
            printf(COL_BLK "%-12s" COL_RESET, historique[i]);
        }
        
        printf("                    " COL_CYN "║\n" COL_RESET);
    }
    
    printf(COL_CYN "╚════════════════════════════════════════╝\n" COL_RESET);
}

void afficher_jeu_complet(struct config *conf, char *dernier_coup, int num_coup, int player, int use_unicode) {
    printf("\033[2J\033[1;1H");
    
    printf(COL_MAG "\n");
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║                                                          ║\n");
    printf("  ║         ♔ ♕ ♖ CHESS ENGINE - ALPHA BETA ♜ ♛ ♚         ║\n");
    printf("  ║                                                          ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf(COL_RESET "\n");
    
    afficher_echiquier_ameliore(conf, use_unicode);
    afficher_panneau_infos(conf, dernier_coup, num_coup, player);
    
    printf("\n");
}

void afficher_menu_principal() {
    printf("\033[2J\033[1;1H");
    
    printf(COL_MAG "\n");
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║                                                          ║\n");
    printf("  ║         ♔ ♕ ♖ CHESS ENGINE - ALPHA BETA ♜ ♛ ♚         ║\n");
    printf("  ║                                                          ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf(COL_RESET "\n");
    
    printf(COL_CYN "  ╔════════════════════════════════════════╗\n");
    printf("  ║" COL_WHT "          GAME MODE SELECTION           " COL_CYN "║\n");
    printf("  ╠════════════════════════════════════════╣\n" COL_RESET);
    printf(COL_CYN "  ║ " COL_RESET "1. " COL_GRN "PC (White) vs PC (Black)       " COL_CYN "║\n" COL_RESET);
    printf(COL_CYN "  ║ " COL_RESET "2. " COL_GRN "Human (Black) vs PC (White)    " COL_CYN "║\n" COL_RESET);
    printf(COL_CYN "  ║ " COL_RESET "3. " COL_GRN "Human (White) vs PC (Black)    " COL_CYN "║\n" COL_RESET);
    printf(COL_CYN "  ╚════════════════════════════════════════╝\n" COL_RESET);
    printf("\n");
}

void afficher_progression(int pourcent) {
    printf("\r  " COL_YEL "AI Thinking: [");
    int bars = pourcent / 5;
    for (int i = 0; i < 20; i++) {
        if (i < bars) printf("█");
        else printf("░");
    }
    printf("] %3d%%" COL_RESET, pourcent);
    fflush(stdout);
}

void effacer_ecran() {
    printf("\033[1;1H\033[2J");
}

void afficher_banniere() {
    printf(COL_CYN "==========================================\n");
    printf("       CHESS ENGINE - ALPHA BETA Pruning Based      \n");
    printf("==========================================\n" COL_RESET);
}

int lire_entier(const char* msg, int min, int max) {
    int val;
    char buffer[100];
    while(1) {
        printf("%s", msg);
        if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            if (sscanf(buffer, "%d", &val) == 1) {
                if ((max == 0 && val >= min) || (val >= min && val <= max))
                    return val;
            }
        }
        printf(COL_RED "  Invalid input. Retry.\n" COL_RESET);
    }
}

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

void appliquer_logique_coup(struct config *curr, struct config *next, int sx, int sy, int dx, int dy, int is_white) {
    copier(curr, next);
    
    int row_home = is_white ? 0 : 7;
    int k_x = is_white ? curr->xrB : curr->xrN;
    int k_y = is_white ? curr->yrB : curr->yrN;
    
    if (sx == k_x + 1 && sy == k_y && dy == sy + 2) {
        next->mat[row_home][4] = 0;
        next->mat[row_home][7] = 0;
        next->mat[row_home][6] = is_white ? 'r' : -'r';
        next->mat[row_home][5] = is_white ? 't' : -'t';
        
        if (is_white) { next->xrB = 0; next->yrB = 6; next->roqueB = 'e'; }
        else          { next->xrN = 7; next->yrN = 6; next->roqueN = 'e'; }
    }

    else if (sx == k_x + 1 && sy == k_y && dy == sy - 2) {
        next->mat[row_home][4] = 0;
        next->mat[row_home][0] = 0;
        next->mat[row_home][2] = is_white ? 'r' : -'r';
        next->mat[row_home][3] = is_white ? 't' : -'t';

        if (is_white) { next->xrB = 0; next->yrB = 2; next->roqueB = 'e'; }
        else          { next->xrN = 7; next->yrN = 2; next->roqueN = 'e'; }
    }
    else {
        next->mat[dx-1][dy] = next->mat[sx-1][sy];
        next->mat[sx-1][sy] = 0;

        char p = next->mat[dx-1][dy];
        int promo_row = is_white ? 8 : 1; 
        char pawn_char = is_white ? 'p' : -'p';

        if (dx == promo_row && p == pawn_char) {
            char choice[10];
            printf(COL_YEL "  Pawn promotion! Choose (n=queen, t=rook, f=bishop, c=knight): " COL_RESET);
            if (fgets(choice, sizeof(choice), stdin) == NULL) {
                choice[0] = '\0';
            }

            char new_p;
            switch(choice[0]) {
                case 'c': new_p = 'c'; break;
                case 'f': new_p = 'f'; break;
                case 't': new_p = 't'; break;
                case 'n': new_p = 'n'; break;
                default:  new_p = 'n';  // we default to queen
            }
            next->mat[dx-1][dy] = is_white ? new_p : -new_p;
        }
    }

    if (is_white) {
        if (next->xrN == dx-1 && next->yrN == dy) { next->xrN = -1; next->yrN = -1; }
    } else {
        if (next->xrB == dx-1 && next->yrB == dy) { next->xrB = -1; next->yrB = -1; }
    }
}

int gerer_tour_humain(struct config *conf, struct config *T, char *coup_str, int player_color) {
    char ch[100], sy, dy;
    int sx, dx, n, i, legal = 0;
    int is_white = (player_color == MAX);
    struct config attempt;

    printf(COL_BLU "  Au tour de l'utilisateur '%c' > " COL_RESET, is_white ? 'B' : 'N');
    
    while (1) {
        printf("SrcY SrcX DestY DestX (ex: d2d3): ");
        if (fgets(ch, 100, stdin) == NULL) continue;
        
        if (sscanf(ch, " %c %d %c %d", &sy, &sx, &dy, &dx) != 4) {
             if (sscanf(ch, " %c%d%c%d", &sy, &sx, &dy, &dx) != 4) {
                 printf(COL_RED "  Format incorrect. Reessayer.\n" COL_RESET);
                 continue;
             }
        }

        appliquer_logique_coup(conf, &attempt, sx, sy-'a', dx, dy-'a', is_white);

        generer_successeurs(conf, player_color, T, &n);
        
        legal = 0;
        int match_idx = -1;
        for (i = 0; i < n; i++) {
            if (configurations_egales(T[i].mat, attempt.mat)) {
                legal = 1;
                match_idx = i;
                break;
            }
        }

        if (legal) {
            printf(COL_GRN "  Coup Valide.\n\n" COL_RESET);
            formuler_coup(conf, &T[match_idx], coup_str);
            copier(&T[match_idx], conf);
            return 1;
        } else {
            if (n == 0) return 0;
            printf(COL_RED "  Coup illegal (%c%d%c%d) -- Reessayer\n" COL_RESET, sy, sx, dy, dx);
        }
    }
}

int gerer_tour_pc(struct config *conf, struct config *T, char *coup_str, int player_color,
                   int depth, int width, int est_func) {

    int n;
    int is_white = (player_color == MAX);
    ResultatRecherche res;

    printf(COL_YEL "  Au tour du PC '%c' (Reflexion...)\n" COL_RESET, is_white ? 'B' : 'N');

    if (!chercher_meilleur_coup(conf, player_color, depth, width, est_func, RECHERCHE_PARALLELE, T, &n, &res)) {
        return 0;
    }

    printf("  H=%d | Alternatives=%d | %.2f s | %lld noeuds | %d threads\n",
           depth, n, res.seconds, res.nodes, res.threads);
    printf(COL_GRN "  Choix=%d (Score: %d)\n\n" COL_RESET, res.best_index + 1, res.score);
    formuler_coup(conf, &T[res.best_index], coup_str);
    copier(&T[res.best_index], conf);
    conf->val = res.score;
    return 1;
}

int main( int argc, char *argv[] )
{
   int typeExec, estMin, estMax, hauteur, largeur;
   char coup[20] = "";
   char nomf[20];
   char buffer[100];

   (void)argc;
   (void)argv;
   
   afficher_menu_principal();

   struct config T[MAX_MOVES], conf;

   typeExec = lire_entier("  Your choice: ", 1, 3);

   initialiser_variables_globales();
   printf("OpenMP threads: %d (processors: %d)\n", omp_get_max_threads(), omp_get_num_procs());
   effacer_ecran();
   afficher_banniere();
   printf("\nFonctions d'estimations:\n");
   printf(" 1. Nombre de pieces\n 2. Occupation + Defense + Roques\n 3. Perturbation aleatoire\n");
   printf(" 4. Menaces\n 5. Occupation simple\n 6. Combinee (2->5->4)\n 7. Aleatoire\n\n");

   if (typeExec != 3) estMax = lire_entier("Est. pour PC Blancs (1-7): ", 1, 7) - 1;
   else estMax = 6;

   if (typeExec != 2) estMin = lire_entier("Est. pour PC Noirs (1-7): ", 1, 7) - 1;
   else estMin = 6;

   printf("\n");
   hauteur = lire_entier("Profondeur d'exploration (ex: 5): ", 1, 20);
   largeur = lire_entier("Largeur du faisceau (0 pour infini): ", 0, 0);
   if (largeur == 0) largeur = +INFINI;

   printf("Nom du fichier de sauvegarde: ");
   if (fgets(buffer, 20, stdin) == NULL || sscanf(buffer, " %s", nomf) != 1) {
      strcpy(nomf, "partie.txt");
   }

   f = fopen(nomf, "w");
   fprintf(f, "--- Estimation_pour_Blancs = %d \t Estimation_pour_Noirs = %d ---\n", 
           estMax+1, estMin+1);

   initialiser_configuration(&conf);
   num_coup = 0;
   PartieLen = 0;
   enregistrer_position(&conf, MAX);
   
   int stop = 0;
   int tour = MAX;
   int success = 0;

   while (!stop) {

      effacer_ecran();
      afficher_banniere();

      sauvegarder_configuration(&conf);

      afficher_jeu_complet(&conf, coup, num_coup, tour, 1);  // we pass 1 for unicode pieces

      if (tour == MAX) {
         if (typeExec == 3) {
             success = gerer_tour_humain(&conf, T, coup, MAX);
         } else {
             success = gerer_tour_pc(&conf, T, coup, MAX, hauteur, largeur, estMax);
         }
         if (!success) stop = 1;
      } 
      else { 
         if (typeExec == 2) {
             success = gerer_tour_humain(&conf, T, coup, MIN);
         } else {
             success = gerer_tour_pc(&conf, T, coup, MIN, hauteur, largeur, estMin);
         }

         if (!success) stop = 1;
      }

      if (stop) {
         int winner = 0;
         int cout = 0;

         if (est_feuille(&conf, tour, &cout)) {
             if (cout > 0) winner = MAX;
             else if (cout < 0) winner = MIN;
         }
         afficher_resultat_et_journaliser(winner);
      } else {
         int winner = 0;
         int next_tour = (tour == MAX ? MIN : MAX);

         num_coup++;
         enregistrer_position(&conf, next_tour);

         if (resoudre_etat_partie(&conf, next_tour, &winner)) {
            stop = 1;
            afficher_resultat_et_journaliser(winner);
         } else {
            tour = next_tour;
         }
         
         if (!stop && typeExec == 1) {
         }
      }
   }

   printf("\nNb de coupes (alpha:%d + beta:%d) = %d\n", nbAlpha, nbBeta, nbAlpha+nbBeta);
   printf("Fin de partie. Appuyez sur Entree pour quitter.\n");
   
   fclose(f);
   while (getchar() != '\n') {
   }
   getchar();

   return 0;
}
