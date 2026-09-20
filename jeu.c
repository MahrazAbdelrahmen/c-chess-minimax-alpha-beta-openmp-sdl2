
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "jeu.h"
#include <stdatomic.h>

// we keep the config history here to detect repeated positions
extern struct config Partie[MAXPARTIE];
extern int PartieMode[MAXPARTIE];
extern int PartieLen;

// we log game history to this file
extern FILE *f;

// number of moves played so far
extern int num_coup;

// initial search depth explored before sorting alternatives
extern int h0;

// available evaluation functions
extern int (*Est[10])(struct config *);

// how many evaluation functions are in the table above
extern int nbEst;

// cutoff counters, for stats
extern int nbAlpha;
extern int nbBeta;

#define TT_SIZE 1048576

struct TTEntry {
    _Atomic uint64_t verif;  // key ^ donnees
    _Atomic uint64_t donnees;
};
struct TTEntry TTable[TT_SIZE];

uint64_t zArray[8][8][12];
uint64_t zBlackMove;  // xor'd in when it's black's turn
uint64_t zCastleB[5];
uint64_t zCastleN[5];

// we map each piece to an index 0-11
int indice_piece(int piece) {
    switch(piece) {
        case 'p': return 0; case -'p': return 6;
        case 'c': return 1; case -'c': return 7;
        case 'f': return 2; case -'f': return 8;
        case 't': return 3; case -'t': return 9;
        case 'n': return 4; case -'n': return 10;
        case 'r': return 5; case -'r': return 11;
        default: return -1;
    }
}

static int indice_etat_roque(char state) {
    switch (state) {
        case 'r': return 0;
        case 'g': return 1;
        case 'p': return 2;
        case 'n': return 3;
        case 'e': return 4;
        default: return 4;
    }
}

static uint64_t cle_tt_pour_trait(struct config *conf, int mode) {
    return conf->hash ^ (mode == MIN ? zBlackMove : 0ULL);
}

void initialiser_zobrist() {
    srand(12345);  // fixed seed so runs are reproducible
    for(int i=0; i<8; i++) {
        for(int j=0; j<8; j++) {
            for(int k=0; k<12; k++) {
                uint64_t r1 = rand();
                uint64_t r2 = rand();
                zArray[i][j][k] = (r1 << 32) | r2;
            }
        }
    }
    zBlackMove = (((uint64_t)rand()) << 32) | (uint64_t)rand();
    for (int i = 0; i < 5; i++) {
        zCastleB[i] = (((uint64_t)rand()) << 32) | (uint64_t)rand();
        zCastleN[i] = (((uint64_t)rand()) << 32) | (uint64_t)rand();
    }
}

// we hash a full position
uint64_t calculer_hash(struct config *conf) {
    uint64_t h = 0;
    for(int i=0; i<8; i++) {
        for(int j=0; j<8; j++) {
            if(conf->mat[i][j] != 0) {
                int idx = indice_piece(conf->mat[i][j]);
                if(idx != -1) h ^= zArray[i][j][idx];
            }
        }
    }
    h ^= zCastleB[indice_etat_roque(conf->roqueB)];
    h ^= zCastleN[indice_etat_roque(conf->roqueN)];
    return h;
}

uint64_t mettre_a_jour_hash(uint64_t hash_courant, struct config *ancienne_conf,
    struct config *nouvelle_conf) {
    (void)hash_courant;
    (void)ancienne_conf;
    return calculer_hash(nouvelle_conf);
}

#define TT_SANS_COUP 0xFFFF

static int cle_coup(struct config *enfant) {
    int cle = (int)(enfant->hash & 0xFFFF);
    return cle == TT_SANS_COUP ? TT_SANS_COUP - 1 : cle;
}

static uint64_t tt_empaqueter(int val, TTFlag flag, int depth, int coup) {
    return (uint64_t)(uint32_t)val
        | ((uint64_t)(uint8_t)depth << 32)
        | ((uint64_t)(uint8_t)flag << 40)
        | ((uint64_t)(uint16_t)coup << 48);
}

void tt_sauvegarder(uint64_t key, int val, TTFlag flag, int depth, int coup) {
    int idx = key % TT_SIZE;
    uint64_t donnees = tt_empaqueter(val, flag, depth, coup < 0 ? TT_SANS_COUP : coup);

    atomic_store_explicit(&TTable[idx].donnees, donnees, memory_order_relaxed);
    atomic_store_explicit(&TTable[idx].verif, key ^ donnees, memory_order_relaxed);
}

bool tt_verifier(uint64_t key, int depth, int alpha, int beta, int *val, int *coup) {
    int idx = key % TT_SIZE;
    uint64_t donnees = atomic_load_explicit(&TTable[idx].donnees, memory_order_relaxed);
    uint64_t verif = atomic_load_explicit(&TTable[idx].verif, memory_order_relaxed);

    *coup = -1;
    if ((verif ^ donnees) != key) return false;

    int entry_val = (int)(int32_t)(uint32_t)donnees;
    int entry_depth = (int)(uint8_t)(donnees >> 32);
    TTFlag entry_flag = (TTFlag)(uint8_t)(donnees >> 40);
    int entry_move = (int)(uint16_t)(donnees >> 48);

    // any entry for this position tells us which move to try first
    if (entry_move != TT_SANS_COUP) *coup = entry_move;

    if (entry_depth != depth) return false;

    if (entry_flag == TT_EXACT) {
        *val = entry_val;
        return true;
    }
    if (entry_flag == TT_ALPHA && entry_val <= alpha) {
        *val = alpha;
        return true;
    }
    if (entry_flag == TT_BETA && entry_val >= beta) {
        *val = beta;
        return true;
    }
    return false;
}

void tt_liberer() {
    memset(TTable, 0, sizeof(TTable));
}

static int dC[8][2] = { {-2,+1} , {-1,+2} , {+1,+2} , {+2,+1} , {+2,-1} , {+1,-2} , {-1,-2} , {-2,-1} };
static int D[8][2] = { {+1,0} , {+1,+1} , {0,+1} , {-1,+1} , {-1,0} , {-1,-1} , {0,-1} , {+1,-1} };

#define MAX_THREADS_COMPTES 256

struct CompteursThread {
    long long noeuds;
    long long coupes_alpha;
    long long coupes_beta;
    char pad[64 - 3 * sizeof(long long)];
};
static struct CompteursThread compteurs[MAX_THREADS_COMPTES];

static struct CompteursThread *compteurs_thread(void) {
    int t = omp_get_thread_num();
    return &compteurs[t < MAX_THREADS_COMPTES ? t : MAX_THREADS_COMPTES - 1];
}

static void compter_coupure(int mode) {
    if (mode == MAX) compteurs_thread()->coupes_beta++;
    else compteurs_thread()->coupes_alpha++;
}

static _Atomic int arret_assistants = 0;

static _Atomic int recherche_annulee = 0;

void annuler_recherche(int annuler) {
    atomic_store(&recherche_annulee, annuler);
}

static int arret_demande(void) {
    if (atomic_load_explicit(&recherche_annulee, memory_order_relaxed)) return 1;
    return atomic_load_explicit(&arret_assistants, memory_order_relaxed) && omp_get_thread_num() != 0;
}

static int meilleur_pour(int mode, int a, int b) {
    return (mode == MAX) ? (a > b) : (a < b);
}

static int coupure(int mode, int score, int alpha, int beta) {
    return (mode == MAX) ? (score >= beta) : (score <= alpha);
}

#ifndef NIV_TRI_MIN
#define NIV_TRI_MIN 2
#endif

static int ordonner_coups(struct config T[], int n, int mode, int niv, int largeur, int numFctEst,
                          int coup_tt) {
    if (largeur != +INFINI || niv >= NIV_TRI_MIN) {
        for (int i = 0; i < n; i++) T[i].val = Est[numFctEst](&T[i]);
        qsort(T, n, sizeof(struct config), (mode == MAX) ? comparer_config_321 : comparer_config_123);
        if (largeur < n) n = largeur;
    }

    if (coup_tt >= 0) {
        for (int i = 1; i < n; i++) {
            if (cle_coup(&T[i]) == coup_tt) {
                struct config meilleur = T[i];
                memmove(&T[1], &T[0], i * sizeof(struct config));
                T[0] = meilleur;
                break;
            }
        }
    }
    return n;
}

typedef struct {
    int niv;
    int npc;  // piece count handed to the children as their 'npp'
    int extension;  // 1 when this node was pushed past the horizon by a capture
    int alpha_orig;
    int beta_orig;
    uint64_t cle;
} Noeud;

static int preparer_noeud(struct config *conf, int mode, int alpha, int beta, int largeur, int numFctEst,
                          int npp, Noeud *nd, struct config T[], int *n, int *valeur) {
    int coup_tt = -1;

    compteurs_thread()->noeuds++;
    nd->cle = cle_tt_pour_trait(conf, mode);
    nd->extension = 0;
    nd->alpha_orig = alpha;
    nd->beta_orig = beta;

    // a captured king ends the game
    if (conf->xrB == -1) { *valeur = -100; return 1; }
    if (conf->xrN == -1) { *valeur = +100; return 1; }

    nd->npc = nombre_pieces(conf);
    if (nd->niv == 0) {
        if (npp == nd->npc || npp == 0) {
            if (!est_feuille(conf, mode, valeur)) *valeur = Est[numFctEst](conf);
            return 1;
        }
        nd->npc = 0;
        nd->niv = 1;
        nd->extension = 1;
    } else if (tt_verifier(nd->cle, nd->niv, alpha, beta, valeur, &coup_tt)) {
        return 1;
    }

    generer_successeurs(conf, mode, T, n);
    if (*n == 0) {
        if (mode == MAX) *valeur = case_menacee_par(MIN, conf->xrB, conf->yrB, conf) ? -100 : 0;
        else *valeur = case_menacee_par(MAX, conf->xrN, conf->yrN, conf) ? +100 : 0;
        return 1;
    }

    *n = ordonner_coups(T, *n, mode, nd->niv, largeur, numFctEst, coup_tt);
    return 0;
}

static void enregistrer_noeud(Noeud *nd, int mode, int score, int coup) {
    if (nd->extension) return;

    TTFlag flag = TT_EXACT;
    if (score <= nd->alpha_orig) flag = TT_ALPHA;
    else if (score >= nd->beta_orig) flag = TT_BETA;

    if ((mode == MAX && flag == TT_ALPHA) || (mode == MIN && flag == TT_BETA)) coup = -1;

    tt_sauvegarder(nd->cle, score, flag, nd->niv, coup);
}

static int borner_infini(int score) {
    if (score == +INFINI) return +100;
    if (score == -INFINI) return -100;
    return score;
}

int minmax_alpha_beta( struct config *conf, int mode, int niv, int alpha, int beta, int largeur, int numFctEst, int npp )
{
   struct config T[MAX_MOVES];
   Noeud nd = { .niv = niv };
   int n, score, best = 0;

   if (arret_demande()) return 0;
   if (preparer_noeud(conf, mode, alpha, beta, largeur, numFctEst, npp, &nd, T, &n, &score)) return score;

   score = (mode == MAX) ? -INFINI : +INFINI;
   for (int i = 0; i < n; i++) {
      int value;
      if (mode == MAX) {
         value = minmax_alpha_beta(&T[i], MIN, nd.niv - 1, (score > alpha) ? score : alpha, beta,
                                   largeur, numFctEst, nd.npc);
      } else {
         value = minmax_alpha_beta(&T[i], MAX, nd.niv - 1, alpha, (score < beta) ? score : beta,
                                   largeur, numFctEst, nd.npc);
      }

      if (arret_demande()) return 0;

      if (meilleur_pour(mode, value, score)) {
         score = value;
         best = i;
      }
      if (coupure(mode, score, alpha, beta)) {
         compter_coupure(mode);
         enregistrer_noeud(&nd, mode, score, cle_coup(&T[i]));
         return score;
      }
   }

   score = borner_infini(score);
   enregistrer_noeud(&nd, mode, score, cle_coup(&T[best]));
   return score;
}

#ifndef YBW_NIV_MIN
#define YBW_NIV_MIN 4
#endif

static int minmax_ybw(struct config *conf, int mode, int niv, int alpha, int beta, int largeur,
                      int numFctEst, int npp) {
    if (niv < YBW_NIV_MIN) return minmax_alpha_beta(conf, mode, niv, alpha, beta, largeur, numFctEst, npp);
    if (arret_demande()) return 0;

    struct config T[MAX_MOVES];
    Noeud nd = { .niv = niv };
    int n, score;

    if (preparer_noeud(conf, mode, alpha, beta, largeur, numFctEst, npp, &nd, T, &n, &score)) return score;

    int first = minmax_ybw(&T[0], -mode, nd.niv - 1, alpha, beta, largeur, numFctEst, nd.npc);
    if (arret_demande()) return 0;
    if (coupure(mode, first, alpha, beta)) {
        compter_coupure(mode);
        enregistrer_noeud(&nd, mode, first, cle_coup(&T[0]));
        return first;
    }

    _Atomic int meilleur = first;
    _Atomic int meilleur_idx = 0;
    _Atomic int coupe = 0;

    for (int i = 1; i < n; i++) {
        #pragma omp task firstprivate(i) shared(T, meilleur, meilleur_idx, coupe)
        {
            if (!atomic_load(&coupe)) {
                int bound = atomic_load(&meilleur);
                int value;
                if (mode == MAX) {
                    value = minmax_ybw(&T[i], MIN, nd.niv - 1, bound > alpha ? bound : alpha, beta,
                                       largeur, numFctEst, nd.npc);
                } else {
                    value = minmax_ybw(&T[i], MAX, nd.niv - 1, alpha, bound < beta ? bound : beta,
                                       largeur, numFctEst, nd.npc);
                }

                int current = atomic_load(&meilleur);
                while (meilleur_pour(mode, value, current)) {
                    if (atomic_compare_exchange_weak(&meilleur, &current, value)) {
                        atomic_store(&meilleur_idx, i);
                        break;
                    }
                }
                if (coupure(mode, value, alpha, beta)) atomic_store(&coupe, 1);
            }
        }
    }
    #pragma omp taskwait
    if (arret_demande()) return 0;

    score = atomic_load(&meilleur);
    if (atomic_load(&coupe)) compter_coupure(mode);
    enregistrer_noeud(&nd, mode, score, cle_coup(&T[atomic_load(&meilleur_idx)]));
    return score;
}

typedef int (*FonctionRecherche)(struct config *, int, int, int, int, int, int, int);

static int evaluer_coup_racine(FonctionRecherche recherche, struct config *coup, int mode, int niv, int borne,
                               int largeur, int numFctEst, int nbp) {
    if (mode == MAX) return recherche(coup, MIN, niv, borne, +INFINI, largeur, numFctEst, nbp);
    return recherche(coup, MAX, niv, -INFINI, borne, largeur, numFctEst, nbp);
}

static void racine_sequentielle(struct config T[], int n, int mode, int niv, int largeur,
                                int numFctEst, int nbp, int *best_index, int *best_score) {
    *best_index = 0;
    *best_score = minmax_alpha_beta(&T[0], -mode, niv, -INFINI, +INFINI, largeur, numFctEst, nbp);

    for (int i = 1; i < n; i++) {
        int value = evaluer_coup_racine(minmax_alpha_beta, &T[i], mode, niv, *best_score, largeur, numFctEst, nbp);
        if (meilleur_pour(mode, value, *best_score)) {
            *best_score = value;
            *best_index = i;
        }
    }
}

static void proposer_coup_racine(int mode, int i, int value, int bound, int *best, int *best_idx,
                                 _Atomic int *borne) {
    if (!meilleur_pour(mode, value, bound)) return;

    #pragma omp critical(racine_parallele)
    {
        if (meilleur_pour(mode, value, *best) || (value == *best && i < *best_idx)) {
            *best = value;
            *best_idx = i;
            if (meilleur_pour(mode, *best, atomic_load(borne))) atomic_store(borne, *best);
        }
    }
}

static int borne_elargie(int mode, int bound) {
    if (bound == INFINI || bound == -INFINI) return bound;
    return bound - mode;
}

static void racine_parallele(struct config T[], int n, int mode, int niv, int largeur,
                             int numFctEst, int nbp, int *best_index, int *best_score) {
    int best = minmax_alpha_beta(&T[0], -mode, niv, -INFINI, +INFINI, largeur, numFctEst, nbp);
    int best_idx = 0;
    _Atomic int borne = best;

    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 1; i < n; i++) {
        int bound = borne_elargie(mode, atomic_load(&borne));
        int value = evaluer_coup_racine(minmax_alpha_beta, &T[i], mode, niv, bound, largeur, numFctEst, nbp);
        proposer_coup_racine(mode, i, value, bound, &best, &best_idx, &borne);
    }

    *best_index = best_idx;
    *best_score = best;
}

static void racine_ybw(struct config T[], int n, int mode, int niv, int largeur,
                       int numFctEst, int nbp, int *best_index, int *best_score) {
    int best = 0;
    int best_idx = 0;

    #pragma omp parallel shared(best, best_idx)
    #pragma omp single
    {
        best = minmax_ybw(&T[0], -mode, niv, -INFINI, +INFINI, largeur, numFctEst, nbp);
        _Atomic int borne = best;

        for (int i = 1; i < n; i++) {
            #pragma omp task firstprivate(i) shared(T, borne, best, best_idx)
            {
                int bound = borne_elargie(mode, atomic_load(&borne));
                int value = evaluer_coup_racine(minmax_ybw, &T[i], mode, niv, bound, largeur, numFctEst, nbp);
                proposer_coup_racine(mode, i, value, bound, &best, &best_idx, &borne);
            }
        }
        #pragma omp taskwait
    }

    *best_index = best_idx;
    *best_score = best;
}

static void racine_lazy_smp(struct config T[], int n, int mode, int niv, int largeur,
                            int numFctEst, int nbp, int *best_index, int *best_score) {
    atomic_store(&arret_assistants, 0);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();

        if (tid == 0) {
            racine_sequentielle(T, n, mode, niv, largeur, numFctEst, nbp, best_index, best_score);
            atomic_store(&arret_assistants, 1);
        } else if (n > 1) {
            int helpers = omp_get_num_threads() - 1;
            int start = (int)((long long)(tid - 1) * (n - 1) / helpers);

            int bound = (mode == MAX) ? -INFINI : +INFINI;
            for (int k = 0; k < n - 1 && !arret_demande(); k++) {
                int i = 1 + (start + k) % (n - 1);
                int value = evaluer_coup_racine(minmax_alpha_beta, &T[i], mode, niv, bound, largeur, numFctEst, nbp);
                if (!arret_demande() && meilleur_pour(mode, value, bound)) bound = value;
            }
        }
    }

    atomic_store(&arret_assistants, 0);
}

int chercher_meilleur_coup(struct config *conf, int mode, int niv, int largeur, int numFctEst,
                           TypeRecherche type, struct config T[], int *n, ResultatRecherche *res) {
    int nbp = nombre_pieces(conf);
    int count;
    double start;

    memset(res, 0, sizeof(*res));
    res->best_index = -1;
    res->threads = (type == RECHERCHE_SEQUENTIELLE) ? 1 : omp_get_max_threads();
    if (largeur <= 0) largeur = +INFINI;

    generer_successeurs(conf, mode, T, n);
    if (*n == 0) return 0;

    for (int i = 0; i < *n; i++) T[i].val = Est[numFctEst](&T[i]);
    qsort(T, *n, sizeof(struct config), (mode == MAX) ? comparer_config_321 : comparer_config_123);
    count = (largeur < *n) ? largeur : *n;

    tt_liberer();
    memset(compteurs, 0, sizeof(compteurs));

    start = omp_get_wtime();
    switch (type) {
        case RECHERCHE_PARALLELE:
            racine_parallele(T, count, mode, niv, largeur, numFctEst, nbp, &res->best_index, &res->score);
            break;
        case RECHERCHE_YBW:
            racine_ybw(T, count, mode, niv, largeur, numFctEst, nbp, &res->best_index, &res->score);
            break;
        case RECHERCHE_LAZY_SMP:
            racine_lazy_smp(T, count, mode, niv, largeur, numFctEst, nbp, &res->best_index, &res->score);
            break;
        default:
            racine_sequentielle(T, count, mode, niv, largeur, numFctEst, nbp, &res->best_index, &res->score);
            break;
    }
    res->seconds = omp_get_wtime() - start;

    for (int t = 0; t < MAX_THREADS_COMPTES; t++) {
        res->nodes += compteurs[t].noeuds;
        res->cutoffs += compteurs[t].coupes_alpha + compteurs[t].coupes_beta;
        nbAlpha += (int)compteurs[t].coupes_alpha;
        nbBeta += (int)compteurs[t].coupes_beta;
    }

    return 1;
}

// evaluation functions

int est_feuille( struct config *conf, int mode, int *cout )
{

   *cout = 0;

   // black wins
   if ( conf->xrB == -1 ) {
      *cout = -100;
      return 1;
   }

   // white wins
   if ( conf->xrN == -1 ) {
      *cout = +100;
      return 1;
   }

   if ( conf->xrB != -1 && conf->xrN != -1 && aucun_coup_possible(conf, mode) ) {
      if (mode == MAX && case_menacee_par(MIN, conf->xrB, conf->yrB, conf)) {
         *cout = -100;
      } else if (mode == MIN && case_menacee_par(MAX, conf->xrN, conf->yrN, conf)) {
         *cout = +100;
      }
      return 1;
   }

   return 0;

}  // fin de est_feuille

// this estimation is based only on the number of pieces
int estimation_1( struct config *conf )
{

        int i, j, ScrQte;
        int pionB = 0, pionN = 0, cfB = 0, cfN = 0, tB = 0, tN = 0, nB = 0, nN = 0;

        for (i=0; i<8; i++)
           for (j=0; j<8; j++) {
                switch (conf->mat[i][j]) {
                   case 'p' : pionB++;   break;
                   case 'c' :
                   case 'f' : cfB++;  break;
                   case 't' : tB++; break;
                   case 'n' : nB++;  break;

                   case -'p' : pionN++;  break;
                   case -'c' :
                   case -'f' : cfN++;  break;
                   case -'t' : tN++; break;
                   case -'n' : nN++;  break;
                }
           }

        ScrQte = ( (pionB*2 + cfB*6 + tB*8 + nB*20) - (pionN*2 + cfN*6 + tN*8 + nN*20) ) * 100.0/76;

        if (ScrQte > 95) ScrQte = 95;
        if (ScrQte < -95) ScrQte = -95;

        return ScrQte;

}  // fin de estimation_1

int estimation_2(  struct config *conf )
{
        int i, j, a, b, stop, bns, ScrQte, ScrDisp, ScrDfs, ScrDivers, Score;
        int pionB = 0, pionN = 0, cfB = 0, cfN = 0, tB = 0, tN = 0, nB = 0, nN = 0;
        int occCentreB = 0, occCentreN = 0, protectRB = 0, protectRN = 0, divB = 0, divN = 0;

        for (i=0; i<8; i++)
           for (j=0; j<8; j++) {
                bns = 0;  // center-occupation bonus
                if (i>1 && i<6 && j>=0 && j<=7 ) bns = 1;
                if (i>2 && i<5 && j>=2 && j<=5 ) bns = 2;
                switch (conf->mat[i][j]) {
                   case 'p' : pionB++; occCentreB += bns;  break;
                   case 'c' :
                   case 'f' : cfB++; occCentreB += 4*bns; break;
                   case 't' : tB++; break;
                   case 'n' : nB++; occCentreB += 4*bns; break;

                   case -'p' : pionN++; occCentreN += bns; break;
                   case -'c' :
                   case -'f' : cfN++; occCentreN += 4*bns; break;
                   case -'t' : tN++; break;
                   case -'n' : nN++; occCentreN += 4*bns; break;
                }
           }

        ScrQte = ( (pionB*2 + cfB*6 + tB*8 + nB*20) - (pionN*2 + cfN*6 + tN*8 + nN*20) );

        ScrDisp = occCentreB - occCentreN;

        // king safety, white
        for (i=0; i<8; i += 1) {
           stop = 0;
           a = conf->xrB + D[i][0];
           b = conf->yrB + D[i][1];
           while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                if ( conf->mat[a][b] != 0 )  stop = 1;
                else {
                    a = a + D[i][0];
                    b = b + D[i][1];
                }
           if ( stop )
                if ( conf->mat[a][b] > 0 ) protectRB++;
        }  // for

        // king safety, black
        for (i=0; i<8; i += 1) {
           stop = 0;
           a = conf->xrN + D[i][0];
           b = conf->yrN + D[i][1];
           while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                if ( conf->mat[a][b] != 0 )  stop = 1;
                else {
                    a = a + D[i][0];
                    b = b + D[i][1];
                }
           if ( stop )
                if ( conf->mat[a][b] < 0 ) protectRN++;
        }  // for

        ScrDfs = protectRB - protectRN;

        if ( conf->roqueB == 'e' ) divB = 24;  // reward white's castling rights
        if ( conf->roqueB == 'r' ) divB = 12;
        if ( conf->roqueB == 'p' || conf->roqueB == 'g' ) divB = 10;

        if ( conf->roqueN == 'e' ) divN = 24;  // reward black's castling rights
        if ( conf->roqueN == 'r' ) divN = 12;
        if ( conf->roqueN == 'p' || conf->roqueN == 'g' ) divN = 10;

        ScrDivers = divB - divN;

        Score = (4*ScrQte + ScrDisp + ScrDfs + ScrDivers) * 100.0/(4*76+42+8+24);
        // see estimation_1 for the piece weights and scaling factor

        if (Score > 98 ) Score = 98;
        if (Score < -98 ) Score = -98;

        return Score;

}  // fin de estimation_2

// material count with a small random perturbation
int estimation_3( struct config *conf )
{

        int i, j, ScrQte, Score;
        int pionB = 0, pionN = 0, cfB = 0, cfN = 0, tB = 0, tN = 0, nB = 0, nN = 0;

        for (i=0; i<8; i++)
           for (j=0; j<8; j++) {
                switch (conf->mat[i][j]) {
                   case 'p' : pionB++; break;
                   case 'c' :
                   case 'f' : cfB++; break;
                   case 't' : tB++; break;
                   case 'n' : nB++; break;

                   case -'p' : pionN++; break;
                   case -'c' :
                   case -'f' : cfN++; break;
                   case -'t' : tN++; break;
                   case -'n' : nN++; break;
                }
           }

        ScrQte = ( (pionB*2 + cfB*6 + tB*8 + nB*20) - (pionN*2 + cfN*6 + tN*8 + nN*20) );

        Score = (10*ScrQte + rand()%10) * 100.0 / (10*76+10);
        // see estimation_1 for the piece weights and scaling factor

        if (Score > 98 ) Score = 98;
        if (Score < -98 ) Score = -98;

        return Score;

}  // fin de estimation_3

// material count plus threatened pieces
int estimation_4( struct config *conf )
{

        int i, j, Score;
        int pionB = 0, pionN = 0, cfB = 0, cfN = 0, tB = 0, tN = 0, nB = 0, nN = 0;
        int npmB = 0, npmN = 0;

        for (i=0; i<8; i++)
           for (j=0; j<8; j++) {
                switch (conf->mat[i][j]) {
                   case 'p' : pionB++;   break;
                   case 'c' :
                   case 'f' : cfB++;  break;
                   case 't' : tB++; break;
                   case 'n' : nB++;  break;

                   case -'p' : pionN++;  break;
                   case -'c' :
                   case -'f' : cfN++;  break;
                   case -'t' : tN++; break;
                   case -'n' : nN++;  break;
                }
           }

        for (i=0; i<8; i++)
           for (j=0; j<8; j++) {
                if ( conf->mat[i][j] < 0 && case_menacee_par(MAX, i, j, conf) ) {
                   npmB++;
                   if ( conf->mat[i][j] == -'c' || conf->mat[i][j] == -'f' )
                           npmB++;
                   if ( conf->mat[i][j] == -'t' || conf->mat[i][j] == -'n' )
                           npmB += 2;
                   if ( conf->mat[i][j] == -'r' )
                           npmB += 5;
                }
                if ( conf->mat[i][j] > 0 && case_menacee_par(MIN, i, j, conf) ) {
                   npmN++;
                   if ( conf->mat[i][j] == 'c' || conf->mat[i][j] == 'f' )
                           npmN++;
                   if ( conf->mat[i][j] == 't' || conf->mat[i][j] == 'n' )
                           npmN += 2;
                   if ( conf->mat[i][j] == 'r' )
                           npmN += 5;
                }
           }

        Score = ( 4*((pionB*2 + cfB*6 + tB*8 + nB*20) - (pionN*2 + cfN*6 + tN*8 + nN*20)) + \
                  (npmB - npmN) ) * 100.0/(4*76+31);

        // see estimation_1 for the piece weights and scaling factor

        if (Score > 95) Score = 95;
        if (Score < -95) Score = -95;

        return Score;

}  // fin de estimation_4

// material count plus center occupation
int estimation_5(  struct config *conf )
{
        int i, j, bns, ScrQte, ScrDisp, Score;
        int pionB = 0, pionN = 0, cfB = 0, cfN = 0, tB = 0, tN = 0, nB = 0, nN = 0;
        int occCentreB = 0, occCentreN = 0;

        for (i=0; i<8; i++)
           for (j=0; j<8; j++) {
                bns = 0;  // center-occupation bonus
                if (i>1 && i<6 && j>=0 && j<=7 ) bns = 1;
                if (i>2 && i<5 && j>=2 && j<=5 ) bns = 2;
                switch (conf->mat[i][j]) {
                   case 'p' : pionB++; occCentreB += bns;  break;
                   case 'c' :
                   case 'f' : cfB++; occCentreB += 4*bns; break;
                   case 't' : tB++; break;
                   case 'n' : nB++; occCentreB += 4*bns; break;

                   case -'p' : pionN++; occCentreN += bns; break;
                   case -'c' :
                   case -'f' : cfN++; occCentreN += 4*bns; break;
                   case -'t' : tN++; break;
                   case -'n' : nN++; occCentreN += 4*bns; break;
                }
           }

        ScrQte = ( (pionB*2 + cfB*6 + tB*8 + nB*20) - (pionN*2 + cfN*6 + tN*8 + nN*20) );

        ScrDisp = occCentreB - occCentreN;

        Score = (4*ScrQte + ScrDisp) * 100.0/(4*76+42);
        // see estimation_1 for the piece weights and scaling factor

        if (Score > 98 ) Score = 98;
        if (Score < -98 ) Score = -98;

        return Score;

}  // fin de estimation_5

// we can also combine several estimations, as in this example
int estimation_6( struct config *conf )
{
        if ( num_coup < 25 )
           return estimation_2(conf);
        if ( num_coup >= 25 && num_coup < 35 )
           return estimation_5(conf);
        if ( num_coup >= 35 )
           return estimation_4(conf);

        return estimation_4(conf);

}  // fin de estimation_6

// a simple random evaluation function
int estimation_7( struct config *conf )
{
        (void)conf;
        return (rand() % 200) - 100;

}  // fin de estimation_7

// move generation

void generer_successeurs( struct config *conf, int mode, struct config T[], int *n )
{
    int i, j, k;

    *n = 0;

    if ( mode == MAX ) {
       for (i=0; i<8; i++)
          for (j=0; j<8; j++)
             if ( conf->mat[i][j] > 0 )
                deplacements_blancs(conf, i, j, T, n );
    }
    else {
       for (i=0; i<8; i++)
          for (j=0; j<8; j++)
             if ( conf->mat[i][j] < 0 )
                deplacements_noirs(conf, i, j, T, n );
    }

    for (k=0; k < *n; k++) {

        T[k].hash = calculer_hash(&T[k]);

        // a move is illegal if it leaves our own king in check
        int estIllegal = 0;

        if (mode == MAX) {
            if ( case_menacee_par( MIN, T[k].xrB, T[k].yrB, &T[k] ) ) estIllegal = 1;
        } else {
            if ( case_menacee_par( MAX, T[k].xrN, T[k].yrN, &T[k] ) ) estIllegal = 1;
        }

        if ( estIllegal ) {
            T[k] = T[(*n)-1];
            (*n)--;
            k--;  // we re-check this slot since it now holds a different move
        }
    }

}  // fin de generer_successeurs

void transformer_pion( struct config *conf, int a, int b, int x, int y, struct config T[], int *n )
{
        int signe = +1;
        if (conf->mat[a][b] < 0 ) signe = -1;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 'n';  // promote to queen
        (*n)++;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 'c';  // promote to knight
        (*n)++;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 'f';  // promote to bishop
        (*n)++;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 't';  // promote to rook
        (*n)++;

}  // fin de transformer_pion

void deplacements_noirs(struct config *conf, int x, int y, struct config T[], int *n )
{
        int i, a, b, stop;

        switch(conf->mat[x][y]) {
        // pawn
        case -'p' :
                if ( x > 0 && conf->mat[x-1][y] == 0 ) {
                        // one square forward
                        copier(conf, &T[*n]);
                        T[*n].mat[x][y] = 0;
                        T[*n].mat[x-1][y] = -'p';
                        (*n)++;
                        if ( x == 1 ) transformer_pion( conf, x, y, x-1, y, T, n );
                }
                if ( x == 6 && conf->mat[5][y] == 0 && conf->mat[4][y] == 0) {
                        // two squares forward
                        copier(conf, &T[*n]);
                        T[*n].mat[6][y] = 0;
                        T[*n].mat[4][y] = -'p';
                        (*n)++;
                }
                if ( x > 0 && y >0 && conf->mat[x-1][y-1] > 0 ) {
                        // capture right (going down the board)
                        copier(conf, &T[*n]);
                        T[*n].mat[x][y] = 0;
                        T[*n].mat[x-1][y-1] = -'p';
                        // we captured the enemy king
                        if (T[*n].xrB == x-1 && T[*n].yrB == y-1) {
                                T[*n].xrB = -1; T[*n].yrB = -1;
                        }

                        (*n)++;
                        if ( x == 1 ) transformer_pion( conf, x, y, x-1, y-1, T, n );
                }
                if ( x > 0 && y < 7 && conf->mat[x-1][y+1] > 0 ) {
                        // capture left (going down the board)
                        copier(conf, &T[*n]);
                        T[*n].mat[x][y] = 0;
                        T[*n].mat[x-1][y+1] = -'p';
                        // we captured the enemy king
                        if (T[*n].xrB == x-1 && T[*n].yrB == y+1) {
                                T[*n].xrB = -1; T[*n].yrB = -1;
                        }

                        (*n)++;
                        if ( x == 1 ) transformer_pion( conf, x, y, x-1, y+1, T, n );
                }
                break;

        // knight
        case -'c' :
                for (i=0; i<8; i++)
                   if ( x+dC[i][0] <= 7 && x+dC[i][0] >= 0 && y+dC[i][1] <= 7 && y+dC[i][1] >= 0 )
                        if ( conf->mat[ x+dC[i][0] ] [ y+dC[i][1] ] >= 0 )  {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           T[*n].mat[ x+dC[i][0] ][ y+dC[i][1] ] = -'c';
                           // we captured the enemy king
                           if (T[*n].xrB == x+dC[i][0] && T[*n].yrB == y+dC[i][1]) {
                                T[*n].xrB = -1; T[*n].yrB = -1;
                           }

                           (*n)++;
                        }
                break;

        // bishop
        case -'f' :
                for (i=1; i<8; i += 2) {
                   stop = 0;
                   a = x + D[i][0];
                   b = y + D[i][1];
                   while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 ) {
                        if ( conf->mat[ a ] [ b ] < 0 )  stop = 1;
                        else {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           if ( T[*n].mat[a][b] > 0 ) stop = 1;
                           T[*n].mat[a][b] = -'f';
                           // we captured the enemy king
                           if (T[*n].xrB == a && T[*n].yrB == b) { T[*n].xrB = -1; T[*n].yrB = -1; }

                           (*n)++;
                              a = a + D[i][0];
                              b = b + D[i][1];
                        }
                   }  // while
                }  // for
                break;

        // rook
        case -'t' :
                for (i=0; i<8; i += 2) {
                   stop = 0;
                   a = x + D[i][0];
                   b = y + D[i][1];
                   while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 ) {
                        if ( conf->mat[ a ] [ b ] < 0 )  stop = 1;
                        else {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           if ( T[*n].mat[a][b] > 0 ) stop = 1;
                           T[*n].mat[a][b] = -'t';
                           // we captured the enemy king
                           if (T[*n].xrB == a && T[*n].yrB == b) { T[*n].xrB = -1; T[*n].yrB = -1; }

                           if ( conf->roqueN != 'e' && conf->roqueN != 'n' ) {
                              if ( x == 7 && y == 0 && conf->roqueN != 'p')
                                 // queenside castle no longer available
                                    T[*n].roqueN = 'g';
                              else if ( x == 7 && y == 0 )
                                      // no castling left at all
                                         T[*n].roqueN = 'n';

                              if ( x == 7 && y == 7 && conf->roqueN != 'g' )
                                 // kingside castle no longer available
                                    T[*n].roqueN = 'p';
                              else if ( x == 7 && y == 7 )
                                      // no castling left at all
                                         T[*n].roqueN = 'n';
                           }

                           (*n)++;
                              a = a + D[i][0];
                              b = b + D[i][1];
                        }
                   }  // while
                }  // for
                break;

        // queen
        case -'n' :
                for (i=0; i<8; i += 1) {
                   stop = 0;
                   a = x + D[i][0];
                   b = y + D[i][1];
                   while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 ) {
                        if ( conf->mat[ a ] [ b ] < 0 )  stop = 1;
                        else {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           if ( T[*n].mat[a][b] > 0 ) stop = 1;
                           T[*n].mat[a][b] = -'n';
                           // we captured the enemy king
                           if (T[*n].xrB == a && T[*n].yrB == b) { T[*n].xrB = -1; T[*n].yrB = -1; }

                           (*n)++;
                              a = a + D[i][0];
                              b = b + D[i][1];
                        }
                   }  // while
                }  // for
                break;

        // king
        case -'r' :
                // check castling availability
                if ( conf->roqueN != 'n' && conf->roqueN != 'e' ) {
                   if ( conf->roqueN != 'g' && conf->mat[7][1] == 0 \
                        && conf->mat[7][2] == 0 && conf->mat[7][3] == 0 )
                      if ( !case_menacee_par( MAX, 7, 1, conf ) && !case_menacee_par( MAX, 7, 2, conf ) && \
                           !case_menacee_par( MAX, 7, 3, conf ) && !case_menacee_par( MAX, 7, 4, conf ) )  {
                        // queenside castle
                        copier(conf, &T[*n]);
                        T[*n].mat[7][4] = 0;
                        T[*n].mat[7][0] = 0;
                        T[*n].mat[7][2] = -'r'; T[*n].xrN = 7; T[*n].yrN = 2;
                        T[*n].mat[7][3] = -'t';
                        // castling rights are gone after this
                        T[*n].roqueN = 'e';
                        (*n)++;
                      }
                   if ( conf->roqueN != 'p' && conf->mat[7][5] == 0 && conf->mat[7][6] == 0 )
                      if ( !case_menacee_par( MAX, 7, 4, conf ) && !case_menacee_par( MAX, 7, 5, conf ) && \
                           !case_menacee_par( MAX, 7, 6, conf ) )  {
                        // kingside castle
                        copier(conf, &T[*n]);
                        T[*n].mat[7][4] = 0;
                        T[*n].mat[7][7] = 0;
                        T[*n].mat[7][6] = -'r'; T[*n].xrN = 7; T[*n].yrN = 6;
                        T[*n].mat[7][5] = -'t';
                        // castling rights are gone after this
                        T[*n].roqueN = 'e';
                        (*n)++;
                      }
                }

                for (i=0; i<8; i += 1) {
                   a = x + D[i][0];
                   b = y + D[i][1];
                   if ( a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                        if ( conf->mat[a][b] >= 0 ) {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           T[*n].mat[a][b] = -'r'; T[*n].xrN = a; T[*n].yrN = b;
                           // we captured the enemy king
                           if (T[*n].xrB == a && T[*n].yrB == b) {
                              T[*n].xrB = -1;
                              T[*n].yrB = -1;
                           }
                           // castling rights are gone after this
                           T[*n].roqueN = 'n';
                           (*n)++;
                        }
                }  // for
                break;

        }

}  // fin de deplacements_noirs

void deplacements_blancs(struct config *conf, int x, int y, struct config T[], int *n )
{
        int i, a, b, stop;

        switch(conf->mat[x][y]) {
        // pawn
        case 'p' :
                if ( x <7 && conf->mat[x+1][y] == 0 ) {
                        // one square forward
                        copier(conf, &T[*n]);
                        T[*n].mat[x][y] = 0;
                        T[*n].mat[x+1][y] = 'p';
                        (*n)++;
                        if ( x == 6 ) transformer_pion( conf, x, y, x+1, y, T, n );
                }
                if ( x == 1 && conf->mat[2][y] == 0 && conf->mat[3][y] == 0) {
                        // two squares forward
                        copier(conf, &T[*n]);
                        T[*n].mat[1][y] = 0;
                        T[*n].mat[3][y] = 'p';
                        (*n)++;
                }
                if ( x < 7 && y > 0 && conf->mat[x+1][y-1] < 0 ) {
                        // capture left (going up the board)
                        copier(conf, &T[*n]);
                        T[*n].mat[x][y] = 0;
                        T[*n].mat[x+1][y-1] = 'p';
                        // we captured the enemy king
                        if (T[*n].xrN == x+1 && T[*n].yrN == y-1) {
                                T[*n].xrN = -1; T[*n].yrN = -1;
                        }

                        (*n)++;
                        if ( x == 6 ) transformer_pion( conf, x, y, x+1, y-1, T, n );
                }
                if ( x < 7 && y < 7 && conf->mat[x+1][y+1] < 0 ) {
                        // capture right (going up the board)
                        copier(conf, &T[*n]);
                        T[*n].mat[x][y] = 0;
                        T[*n].mat[x+1][y+1] = 'p';
                        // we captured the enemy king
                        if (T[*n].xrN == x+1 && T[*n].yrN == y+1) {
                                T[*n].xrN = -1; T[*n].yrN = -1;
                        }

                        (*n)++;
                        if ( x == 6 ) transformer_pion( conf, x, y, x+1, y+1, T, n );
                }
                break;

        // knight
        case 'c' :
                for (i=0; i<8; i++)
                   if ( x+dC[i][0] <= 7 && x+dC[i][0] >= 0 && y+dC[i][1] <= 7 && y+dC[i][1] >= 0 )
                        if ( conf->mat[ x+dC[i][0] ] [ y+dC[i][1] ] <= 0 )  {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           T[*n].mat[ x+dC[i][0] ][ y+dC[i][1] ] = 'c';
                           // we captured the enemy king
                           if (T[*n].xrN == x+dC[i][0] && T[*n].yrN == y+dC[i][1]) {
                                T[*n].xrN = -1;
                                T[*n].yrN = -1;
                           }

                           (*n)++;
                        }
                break;

        // bishop
        case 'f' :
                for (i=1; i<8; i += 2) {
                   stop = 0;
                   a = x + D[i][0];
                   b = y + D[i][1];
                   while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 ) {
                        if ( conf->mat[ a ] [ b ] > 0 )  stop = 1;
                        else {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           if ( T[*n].mat[a][b] < 0 ) stop = 1;
                           T[*n].mat[a][b] = 'f';
                           // we captured the enemy king
                           if (T[*n].xrN == a && T[*n].yrN == b) {
                                T[*n].xrN = -1;
                                T[*n].yrN = -1;
                           }
                           (*n)++;
                              a = a + D[i][0];
                              b = b + D[i][1];
                        }
                   }  // while
                }  // for
                break;

        // rook
        case 't' :
                for (i=0; i<8; i += 2) {
                   stop = 0;
                   a = x + D[i][0];
                   b = y + D[i][1];
                   while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 ) {
                        if ( conf->mat[ a ] [ b ] > 0 )  stop = 1;
                        else {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           if ( T[*n].mat[a][b] < 0 ) stop = 1;
                           T[*n].mat[a][b] = 't';
                           // we captured the enemy king
                           if (T[*n].xrN == a && T[*n].yrN == b) {
                                T[*n].xrN = -1;
                                T[*n].yrN = -1;
                           }
                           if ( conf->roqueB != 'e' && conf->roqueB != 'n' ) {
                             if ( x == 0 && y == 0 && conf->roqueB != 'p')
                                // queenside castle no longer available
                                   T[*n].roqueB = 'g';
                             else if ( x == 0 && y == 0 )
                                     // no castling left at all
                                        T[*n].roqueB = 'n';
                             if ( x == 0 && y == 7 && conf->roqueB != 'g' )
                                // kingside castle no longer available
                                   T[*n].roqueB = 'p';
                             else if ( x == 0 && y == 7 )
                                     // no castling left at all
                                        T[*n].roqueB = 'n';
                           }

                           (*n)++;
                              a = a + D[i][0];
                              b = b + D[i][1];
                        }
                   }  // while
                }  // for
                break;

        // queen
        case 'n' :
                for (i=0; i<8; i += 1) {
                   stop = 0;
                   a = x + D[i][0];
                   b = y + D[i][1];
                   while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 ) {
                        if ( conf->mat[ a ] [ b ] > 0 )  stop = 1;
                        else {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           if ( T[*n].mat[a][b] < 0 ) stop = 1;
                           T[*n].mat[a][b] = 'n';
                           // we captured the enemy king
                           if (T[*n].xrN == a && T[*n].yrN == b) {
                              T[*n].xrN = -1;
                              T[*n].yrN = -1;
                           }
                           (*n)++;
                              a = a + D[i][0];
                              b = b + D[i][1];
                        }
                   }  // while
                }  // for
                break;

        // king
        case 'r' :
                // check castling availability
                if ( conf->roqueB != 'n' && conf->roqueB != 'e' ) {
                   if ( conf->roqueB != 'g' && conf->mat[0][1] == 0 && \
                        conf->mat[0][2] == 0 && conf->mat[0][3] == 0 )
                      if ( !case_menacee_par( MIN, 0, 1, conf ) && !case_menacee_par( MIN, 0, 2, conf ) && \
                           !case_menacee_par( MIN, 0, 3, conf ) && !case_menacee_par( MIN, 0, 4, conf ) )  {
                        // queenside castle
                        copier(conf, &T[*n]);
                        T[*n].mat[0][4] = 0;
                        T[*n].mat[0][0] = 0;
                        T[*n].mat[0][2] = 'r'; T[*n].xrB = 0; T[*n].yrB = 2;
                        T[*n].mat[0][3] = 't';
                        // castling rights are gone after this
                        T[*n].roqueB = 'e';
                        (*n)++;
                      }
                   if ( conf->roqueB != 'p' && conf->mat[0][5] == 0 && conf->mat[0][6] == 0 )
                      if ( !case_menacee_par( MIN, 0, 4, conf ) && !case_menacee_par( MIN, 0, 5, conf ) && \
                           !case_menacee_par( MIN, 0, 6, conf ) )  {
                        // kingside castle
                        copier(conf, &T[*n]);
                        T[*n].mat[0][4] = 0;
                        T[*n].mat[0][7] = 0;
                        T[*n].mat[0][6] = 'r'; T[*n].xrB = 0; T[*n].yrB = 6;
                        T[*n].mat[0][5] = 't';
                        // castling rights are gone after this
                        T[*n].roqueB = 'e';
                        (*n)++;
                      }
                }

                for (i=0; i<8; i += 1) {
                   a = x + D[i][0];
                   b = y + D[i][1];
                   if ( a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                        if ( conf->mat[a][b] <= 0 ) {
                           copier(conf, &T[*n]);
                           T[*n].mat[x][y] = 0;
                           T[*n].mat[a][b] = 'r'; T[*n].xrB = a; T[*n].yrB = b;
                           // we captured the enemy king
                           if (T[*n].xrN == a && T[*n].yrN == b) {
                              T[*n].xrN = -1;
                              T[*n].yrN = -1;
                           }
                           // castling rights are gone after this
                           T[*n].roqueB = 'n';
                           (*n)++;
                        }
                }  // for
                break;

        }

}  // fin de deplacements_blancs

// utility functions

int case_menacee_par( int mode, int x, int y, struct config *conf )
{
        int i, a, b, stop;

        // threatened by the king
        for (i=0; i<8; i += 1) {
           a = x + D[i][0];
           b = y + D[i][1];
           if ( a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                if ( conf->mat[a][b]*mode == 'r' ) return 1;
        }  // for

        // threatened by a knight
        for (i=0; i<8; i++)
           if ( x+dC[i][0] <= 7 && x+dC[i][0] >= 0 && y+dC[i][1] <= 7 && y+dC[i][1] >= 0 )
                if ( conf->mat[ x+dC[i][0] ] [ y+dC[i][1] ] * mode == 'c' )
                   return 1;

        // threatened by a pawn
        if ( (x-mode) >= 0 && (x-mode) <= 7 && y > 0 && conf->mat[x-mode][y-1]*mode == 'p' )
           return 1;
        if ( (x-mode) >= 0 && (x-mode) <= 7 && y < 7 && conf->mat[x-mode][y+1]*mode == 'p' )
           return 1;

        // threatened by a bishop, rook or queen
        for (i=0; i<8; i += 1) {
           stop = 0;
           a = x + D[i][0];
           b = y + D[i][1];
           while ( !stop && a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                if ( conf->mat[a][b] != 0 )  stop = 1;
                else {
                    a = a + D[i][0];
                    b = b + D[i][1];
                }
           if ( stop )  {
                if ( conf->mat[a][b]*mode == 'f' && i % 2 != 0 ) return 1;
                if ( conf->mat[a][b]*mode == 't' && i % 2 == 0 ) return 1;
                if ( conf->mat[a][b]*mode == 'n' ) return 1;
           }
        }  // for

        return 0;

}  // fin de case_menacee_par

int comparer_config_123(const void *a, const void *b)  // ascending order
{
    int x = ((struct config *)a)->val, y = ((struct config *)b)->val;
    if ( x < y )
        return -1;
    if ( x == y )
        return 0;
    return 1;
}  // fin comparer_config_123

int comparer_config_321(const void *a, const void *b)  // descending order
{
    int x = ((struct config *)a)->val, y = ((struct config *)b)->val;
    if ( x < y )
        return 1;
    if ( x == y )
        return 0;
    return -1;
}  // fin comparer_config_321

// number of pieces belonging to N and B on the board 'conf'
int nombre_pieces( struct config *conf )
{
    int i,j;
    int nb = 0;
    for (i=0; i<8; i++)
        for (j=0; j<8; j++)
            if ( conf->mat[i][j] != 0 ) nb++;
    return nb;
}

// saves conf to file f (for history)
void sauvegarder_configuration( struct config *conf )
{

   char buf[72] = "";
   int i, j;
   for (i=7; i>=0; i--) {
        for (j=0; j<8; j++)
           if (conf->mat[i][j] == 0)  strcat(buf," ");
           else if (conf->mat[i][j] < 0) {
                   strcat(buf, "-");
                   buf[strlen(buf)-1] = -conf->mat[i][j];
                }
                else {
                   strcat(buf, "+");
                   buf[strlen(buf)-1] = conf->mat[i][j] - 32;
                }
        strcat(buf, "\n");
   }
   fprintf(f, "--- coup N %d ---\n%s\n",num_coup, buf);
   fflush(f);

}  // fin sauvegarder_configuration

void formuler_coup( struct config *oldconf, struct config *newconf, char *coup )
{
        int i,j;
        char piece[20];

        // white castled
        if ( newconf->roqueB == 'e' && oldconf->roqueB != 'e' ) {
           if ( newconf->yrB == 2 ) sprintf(coup, "g_roqueB" );
           else  sprintf(coup, "p_roqueB" );
           return;
        }

        // black castled
        if ( newconf->roqueN == 'e' && oldconf->roqueN != 'e' ) {
           if ( newconf->yrN == 2 ) sprintf(coup, "g_roqueN" );
           else  sprintf(coup, "p_roqueN" );
           return;
        }

        for(i=0; i<8; i++)
           for (j=0; j<8; j++)
                if ( oldconf->mat[i][j] != newconf->mat[i][j] )
                   if ( newconf->mat[i][j] != 0 ) {
                        switch (newconf->mat[i][j]) {
                           case -'p' : sprintf(piece,"pionN"); break;
                           case 'p' : sprintf(piece,"pionB"); break;
                           case -'c' : sprintf(piece,"cavalierN"); break;
                           case 'c' : sprintf(piece,"cavalierB"); break;
                           case -'f' : sprintf(piece,"fouN"); break;
                           case 'f' : sprintf(piece,"fouB"); break;
                           case -'t' : sprintf(piece,"tourN"); break;
                           case 't' : sprintf(piece,"tourB"); break;
                           case -'n' : sprintf(piece,"reineN"); break;
                           case 'n' : sprintf(piece,"reineB"); break;
                           case -'r' : sprintf(piece,"roiN"); break;
                           case 'r' : sprintf(piece,"roiB"); break;
                        }
                        sprintf(coup, "%s en %c%d", piece, 'a'+j, i+1);
                        return;
                   }
}  // fin de formuler_coup

// prints the config 'conf'
void afficher_configuration( struct config *conf, char *coup, int num )
{
        int i, j, k;
        int pB = 0, pN = 0, cB = 0, cN = 0, fB = 0, fN = 0, tB = 0, tN = 0, nB = 0, nN = 0;

             printf("Coup num:%3d : %s\n", num, coup);
             printf("\n");
        for (i=0;  i<8; i++)
                printf("\t   %c", i+'a');
           printf("\n");

        for (i=0;  i<8; i++)
                printf("\t-------");
           printf("\n");

        for(i=8; i>0; i--)  {
                printf("    %d", i);
                for (j=0; j<8; j++) {
                        if ( conf->mat[i-1][j] < 0 ) printf("\t  %cN", -conf->mat[i-1][j]);
                        else if ( conf->mat[i-1][j] > 0 ) printf("\t  %cB", conf->mat[i-1][j]);
                                  else printf("\t   ");
                        switch (conf->mat[i-1][j]) {
                            case -'p' : pN++; break;
                            case 'p'  : pB++; break;
                            case -'c' : cN++; break;
                            case 'c'  : cB++; break;
                            case -'f' : fN++; break;
                            case 'f'  : fB++; break;
                            case -'t' : tN++; break;
                            case 't'  : tB++; break;
                            case -'n' : nN++; break;
                            case 'n'  : nB++; break;
                        }
                }
                printf("\n");

                for (k=0;  k<8; k++)
                        printf("\t-------");
                   printf("\n");

        }
        printf("\n\tB : p(%d) c(%d) f(%d) t(%d) n(%d) \t N : p(%d) c(%d) f(%d) t(%d) n(%d)\n\n",
                pB, cB, fB, tB, nB, pN, cN, fN, tN, nN);
        printf("\n");

}  // fin de  afficher_configuration

// tests whether boards c1 and c2 are equal
int configurations_egales(char c1[8][8], char c2[8][8] )
{
        int i, j;

        for (i=0; i<8; i++)
                for (j=0; j<8; j++)
                        if (c1[i][j] != c2[i][j]) return 0;
        return 1;
}  // fin de configurations_egales

void initialiser_configuration( struct config *conf )
{
    static int zobrist_inited = 0;
    if(!zobrist_inited) {
        initialiser_zobrist();
        zobrist_inited = 1;
    }

    int i, j;
    for (i=0; i<8; i++)
        for (j=0; j<8; j++)
            conf->mat[i][j] = 0;

    conf->mat[0][0]='t'; conf->mat[0][1]='c'; conf->mat[0][2]='f'; conf->mat[0][3]='n';
    conf->mat[0][4]='r'; conf->mat[0][5]='f'; conf->mat[0][6]='c'; conf->mat[0][7]='t';

    for (j=0; j<8; j++) {
        conf->mat[1][j] = 'p';
        conf->mat[6][j] = -'p';
        conf->mat[7][j] = -conf->mat[0][j];
    }

    conf->xrB = 0; conf->yrB = 4;
    conf->xrN = 7; conf->yrN = 4;
    conf->roqueB = 'r';
    conf->roqueN = 'r';
    conf->val = 0;

    conf->hash = calculer_hash(conf);
}

// we count repetitions using the recorded game history
int deja_visitee( struct config *conf, int mode )
{
   int count = 0;
   int limit = PartieLen;

   for (int i = 0; i < limit; i++) {
       if (PartieMode[i] != mode) continue;
       // we compare the 64-bit hashes first, since it's fast
       if ( conf->hash == Partie[i].hash ) {
           if ( configurations_egales( conf->mat, Partie[i].mat ) ) count++;
       }
   }
   return count;
}

void copier( struct config *c1, struct config *c2 )
{
    // memcpy is usually faster than a nested loop here
    memcpy(c2->mat, c1->mat, sizeof(c1->mat));

    c2->val = c1->val;
    c2->xrB = c1->xrB;
    c2->yrB = c1->yrB;
    c2->xrN = c1->xrN;
    c2->yrN = c1->yrN;
    c2->roqueB = c1->roqueB;
    c2->roqueN = c1->roqueN;

    c2->hash = c1->hash;
}

// tests whether there is no possible move in config 'conf'
int aucun_coup_possible( struct config *conf, int mode )
{
        struct config T[MAX_MOVES];

        for (int i = 0; i < 8; i++) {
           for (int j = 0; j < 8; j++) {
              int n = 0;

              if (mode == MAX && conf->mat[i][j] > 0) deplacements_blancs(conf, i, j, T, &n);
              else if (mode == MIN && conf->mat[i][j] < 0) deplacements_noirs(conf, i, j, T, &n);

              for (int k = 0; k < n; k++) {
                 if (mode == MAX && !case_menacee_par(MIN, T[k].xrB, T[k].yrB, &T[k])) return 0;
                 if (mode == MIN && !case_menacee_par(MAX, T[k].xrN, T[k].yrN, &T[k])) return 0;
              }
           }
        }
        return 1;

}  // fin de aucun_coup_possible
