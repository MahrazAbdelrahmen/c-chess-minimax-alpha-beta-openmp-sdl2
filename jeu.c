/*
 minmax with alpha-beta pruning for chess.
 original teaching base: Hidouci W.K. / ESI 2025.
 later additions (parallelism, refactors, fixes, and other extensions) are not
 part of that original teaching base.

 functions are grouped into 4 parts:
      - minmax with alpha/beta
      - evaluation functions
      - move generation
      - utility functions
*/

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



/* ==========================================================================
   GLOBAL VARIABLES & ZOBRIST HASHING
   ========================================================================== */

// ~1 million entries; tune based on available RAM
#define TT_SIZE 1048576
struct TTEntry TTable[TT_SIZE];

// random numbers for zobrist hashing: [square][square][piece type]
uint64_t zArray[8][8][12];
uint64_t zBlackMove; // xor'd in when it's black's turn
uint64_t zCastleB[5];
uint64_t zCastleN[5];

// we map each piece to an index 0-11
int indice_piece(int piece) {
    // 0-5: white (p,c,f,t,q,k), 6-11: black
    switch(piece) {
        case 'p': return 0; case -'p': return 6;
        case 'c': return 1; case -'c': return 7;
        case 'f': return 2; case -'f': return 8;
        case 't': return 3; case -'t': return 9;
        case 'n': return 4; case -'n': return 10; // queen uses 'n' (french "reine")
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
    srand(12345); // fixed seed so runs are reproducible
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

/* ==========================================================================
   TRANSPOSITION TABLE MANAGEMENT
   ========================================================================== */

void tt_sauvegarder(uint64_t key, int val, TTFlag flag, int depth, int bestMoveIndex) {
    int idx = key % TT_SIZE;

    // we always replace on collision (no depth-based logic) and skip locking for speed;
    // the small risk of a corrupted entry is acceptable here
    TTable[idx].key = key;
    TTable[idx].value = val;
    TTable[idx].flag = flag;
    TTable[idx].depth = depth;
    TTable[idx].bestMoveIndex = bestMoveIndex;
}

bool tt_verifier(uint64_t key, int depth, int alpha, int beta, int *val, int *bestMoveIndex) {
    int idx = key % TT_SIZE;

    if (TTable[idx].key == key) {
        // we only trust the entry if it was stored at a sufficient depth
        if (TTable[idx].depth >= depth) {
            if (TTable[idx].flag == TT_EXACT) {
                *val = TTable[idx].value;
                *bestMoveIndex = TTable[idx].bestMoveIndex;
                return true;
            }
            if (TTable[idx].flag == TT_ALPHA && TTable[idx].value <= alpha) {
                *val = alpha;
                return true;
            }
            if (TTable[idx].flag == TT_BETA && TTable[idx].value >= beta) {
                *val = beta;
                return true;
            }
        }
    }
    return false;
}

void tt_liberer() {
    memset(TTable, 0, sizeof(TTable));
}

/* internal (static) variables of the "jeu.c" module */

// move vectors per piece type:
//    knight:
static int dC[8][2] = { {-2,+1} , {-1,+2} , {+1,+2} , {+2,+1} , {+2,-1} , {+1,-2} , {-1,-2} , {-2,-1} };
//    bishop (odd indices), rook (even indices), queen/king (both):
static int D[8][2] = { {+1,0} , {+1,+1} , {0,+1} , {-1,+1} , {-1,0} , {-1,-1} , {0,-1} , {+1,-1} };



// minmax with alpha-beta


/* minmax with alpha-beta pruning:
 evaluates 'conf' for player 'mode', descending 'niv' levels.
 'niv' is decremented on each recursive call.
 'alpha' and 'beta' are the bounds we use for pruning.
 'largeur' caps how many alternatives we explore at each level;
 largeur == +INFINI means all alternatives are considered (the default).
 'numFctEst' selects which evaluation function to use once 'niv' reaches 0.
 'npp' is the parent config's piece count, used to detect capture-triggered instability.
*/
/* ==========================================================================
   SEQUENTIAL MINMAX WITH TRANSPOSITION TABLE
   ========================================================================== */

int minmax_alpha_beta( struct config *conf, int mode, int niv, int alpha, int beta, int largeur, int numFctEst, int npp )
{
   int n, i, score, score2, npc;
   struct config T[MAX_MOVES];
   int bestMoveIdx = -1;
   int tt_val, tt_move;
   uint64_t tt_key = cle_tt_pour_trait(conf, mode);

   if (tt_verifier(tt_key, niv, alpha, beta, &tt_val, &tt_move)) {
       return tt_val;
   }

   npc = nombre_pieces(conf);

   if ( est_feuille(conf, mode, &score) )
      return score;

   if ( niv == 0 ) {
      if ( npp == npc || npp == 0 )
         return Est[numFctEst]( conf );
      else {
         npc = 0;
         niv = 1;
      }
   }

   // we keep the original bounds so we can pick the right TT flag below
   int alphaOrig = alpha;
   int betaOrig = beta;

   if ( mode == MAX ) {
      generer_successeurs( conf, MAX, T, &n );

      if ( largeur != +INFINI ) {
         for (i=0; i<n; i++) T[i].val = Est[numFctEst]( &T[i] );
         qsort(T, n, sizeof(struct config), comparer_config_321);
         if ( largeur < n ) n = largeur;
      }

      score = -INFINI; // we start from the lowest possible score
      for ( i=0; i<n; i++ ) {
          score2 = minmax_alpha_beta( &T[i], MIN, niv-1, (score > alpha) ? score : alpha, beta, largeur, numFctEst, npc);

          if (score2 > score) {
              score = score2;
              bestMoveIdx = i; // we remember the best move's index
          }

          if (score >= beta) {
             // beta cutoff: we store a lower bound
             tt_sauvegarder(tt_key, score, TT_BETA, niv, i);
             #pragma omp atomic
             nbBeta++;
             return score;
          }
      }
   }
   else  { // mode == MIN
      generer_successeurs( conf, MIN, T, &n );

      if ( largeur != +INFINI ) {
         for (i=0; i<n; i++) T[i].val = Est[numFctEst]( &T[i] );
         qsort(T, n, sizeof(struct config), comparer_config_123);
         if ( largeur < n ) n = largeur;
      }

      score = +INFINI; // we start from the highest possible score
      for ( i=0; i<n; i++ ) {
          score2 = minmax_alpha_beta( &T[i], MAX, niv-1, alpha, (score < beta) ? score : beta, largeur, numFctEst, npc );

          if (score2 < score) {
              score = score2;
              bestMoveIdx = i;
          }

          if (score <= alpha) {
             // alpha cutoff: we store an upper bound
             tt_sauvegarder(tt_key, score, TT_ALPHA, niv, i);
             #pragma omp atomic
             nbAlpha++;
             return score;
          }
      }
   }

   if ( score == +INFINI ) score = +100;
   if ( score == -INFINI ) score = -100;

   // we store an exact value or a bound depending on how the search finished
   TTFlag flag = TT_EXACT;
   if (score <= alphaOrig) flag = TT_ALPHA;
   else if (score >= betaOrig) flag = TT_BETA;

   tt_sauvegarder(tt_key, score, flag, niv, bestMoveIdx);

   return score;
}



/* ==========================================================================
   PARALLEL SEARCH ALGORITHM (ROOT SPLITTING)
   ========================================================================== */
// remember to raise the stack size before running this: export OMP_STACKSIZE=64M
int minmax_parallele(struct config *conf, int mode, int niv, int alpha, int beta, int largeur, int numFctEst, int npp) {
    struct config T[MAX_MOVES];
    int n, i;
    int npc = nombre_pieces(conf);

    (void)npp;

    conf->hash = calculer_hash(conf);
    generer_successeurs(conf, mode, T, &n);

    if (n == 0) return (mode == MAX) ? -100 : 100;

    // we sort moves so the strongest candidate lands at T[0], which the
    // young-brothers-wait split below relies on
    for (i = 0; i < n; i++) T[i].val = Est[numFctEst](&T[i]);
    qsort(T, n, sizeof(struct config), (mode == MAX) ? comparer_config_321 : comparer_config_123);

    if (largeur < n) n = largeur;

    // young brothers wait (YBW): we search the first move alone to set a
    // solid alpha/beta baseline, so the parallel threads below can prune harder
    int bestScore = minmax_alpha_beta(&T[0], -mode, niv - 1, alpha, beta, largeur, numFctEst, npc);

    if (mode == MAX) {
        if (bestScore > alpha) alpha = bestScore;
        if (alpha >= beta) return alpha;
    } else {
        if (bestScore < beta) beta = bestScore;
        if (beta <= alpha) return beta;
    }

    // we use atomics here so threads can update the shared bounds without a lock
    _Atomic int globalAlpha = alpha;
    _Atomic int globalBeta = beta;
    _Atomic int stopSearch = 0; // signals other threads to stop once a cutoff is found

    // we schedule dynamically with chunk size 1 to balance load across threads
    #pragma omp parallel for schedule(dynamic, 1) shared(globalAlpha, globalBeta, stopSearch)
    for (i = 1; i < n; i++) {

        if (atomic_load(&stopSearch)) continue;

        int myAlpha = atomic_load(&globalAlpha);
        int myBeta = atomic_load(&globalBeta);

        if (myAlpha >= myBeta) continue;

        // pvs: we assume T[0] is the best move and search T[i] with a
        // zero-width window to cheaply try to prove it's worse
        int val;
        int pvs_alpha = myAlpha;
        int pvs_beta = myBeta;

        if (mode == MAX) pvs_beta = myAlpha + 1;
        else pvs_alpha = myBeta - 1;

        val = minmax_alpha_beta(&T[i], -mode, niv - 1, pvs_alpha, pvs_beta, largeur, numFctEst, npc);

        // if the null-window search suggests T[i] beats T[0], we re-search with the full window
        if (mode == MAX) {
            if (val > myAlpha && val < beta) {
                val = minmax_alpha_beta(&T[i], -mode, niv - 1, val, beta, largeur, numFctEst, npc);
            }
        } else {
            if (val < myBeta && val > alpha) {
                val = minmax_alpha_beta(&T[i], -mode, niv - 1, alpha, val, largeur, numFctEst, npc);
            }
        }

        if (mode == MAX) {
            if (val > bestScore) {
                // we use a CAS loop to bump the shared alpha without locking
                int currentA = atomic_load(&globalAlpha);
                while (val > currentA) {
                    if (atomic_compare_exchange_weak(&globalAlpha, &currentA, val)) {
                        break;
                    }
                }
                if (val >= beta) atomic_store(&stopSearch, 1);
            }
        } else {
            if (val < bestScore) { // bestScore itself doesn't need atomic updates for pruning; only the bounds do
                int currentB = atomic_load(&globalBeta);
                while (val < currentB) {
                    if (atomic_compare_exchange_weak(&globalBeta, &currentB, val)) {
                        break;
                    }
                }
                if (val <= alpha) atomic_store(&stopSearch, 1);
            }
        }

        // we update the shared bestScore inside a critical section
        #pragma omp critical
        {
             if (mode == MAX) {
                 if (val > bestScore) bestScore = val;
             } else {
                 if (val < bestScore) bestScore = val;
             }
        }
    }

    return bestScore;
}


// evaluation functions


/* tests whether conf is a terminal position, and returns its value in 'cout' */
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

   // no legal move left: checkmate or stalemate
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


/* a few simple example evaluation functions (estimation_1, estimation_2, ...) */
/* see the 'estim' function further below for how the active one is chosen */

/* this estimation is based only on the number of pieces */
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

        // we use fixed weights: pawn=2 knight/bishop=6 rook=8 queen=20
        // scaled by 100/76 so the score stays inside (-100, +100)
        ScrQte = ( (pionB*2 + cfB*6 + tB*8 + nB*20) - (pionN*2 + cfN*6 + tN*8 + nN*20) ) * 100.0/76;

        if (ScrQte > 95) ScrQte = 95;
        if (ScrQte < -95) ScrQte = -95;

        return ScrQte;

} // fin de estimation_1


// we score material, center occupation, king safety, and castling rights
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
        // max possible: 76

        ScrDisp = occCentreB - occCentreN;
        // max possible: 42

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
        } // for

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
        } // for

        ScrDfs = protectRB - protectRN;
        // max possible: 8

        if ( conf->roqueB == 'e' ) divB = 24;        // reward white's castling rights
        if ( conf->roqueB == 'r' ) divB = 12;
        if ( conf->roqueB == 'p' || conf->roqueB == 'g' ) divB = 10;

        if ( conf->roqueN == 'e' ) divN = 24;        // reward black's castling rights
        if ( conf->roqueN == 'r' ) divN = 12;
        if ( conf->roqueN == 'p' || conf->roqueN == 'g' ) divN = 10;

        ScrDivers = divB - divN;
        // max possible: 24

        Score = (4*ScrQte + ScrDisp + ScrDfs + ScrDivers) * 100.0/(4*76+42+8+24);
        // see estimation_1 for the piece weights and scaling factor

        if (Score > 98 ) Score = 98;
        if (Score < -98 ) Score = -98;

        return Score;

} // fin de estimation_2


/* material count with a small random perturbation */
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
        // max possible: 76

        Score = (10*ScrQte + rand()%10) * 100.0 / (10*76+10);
        // see estimation_1 for the piece weights and scaling factor

        if (Score > 98 ) Score = 98;
        if (Score < -98 ) Score = -98;

        return Score;

} // fin de estimation_3


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

} // fin de estimation_4


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

} // fin de estimation_5


/* we can also combine several estimations, as in this example */
int estimation_6( struct config *conf )
{
        // we blend 3 evaluation functions across the game:
        // - opening: estimation_2, for center control and king safety
        // - midgame: estimation_5, to keep improving position
        // - endgame: estimation_4, to favor attacking
        if ( num_coup < 25 )
           return estimation_2(conf);
        if ( num_coup >= 25 && num_coup < 35 )
           return estimation_5(conf);
        if ( num_coup >= 35 )
           return estimation_4(conf);

        return estimation_4(conf);

} // fin de estimation_6


/* a simple random evaluation function */
int estimation_7( struct config *conf )
{
        (void)conf;
        return (rand() % 200) - 100;

} // fin de estimation_7


// move generation


/* generates, for player 'mode', the successors of 'conf' into array T,
   and returns in 'n' the number of child configurations generated */
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

    // we filter illegal moves, checks and repeated positions, and hash each survivor
    for (k=0; k < *n; k++) {

        // we recompute the hash since the board changed and the old one is now stale
        T[k].hash = calculer_hash(&T[k]);

        // a move is illegal if it leaves our own king in check
        int estIllegal = 0;

        if (mode == MAX) {
            if ( case_menacee_par( MIN, T[k].xrB, T[k].yrB, &T[k] ) ) estIllegal = 1;
        } else {
            if ( case_menacee_par( MAX, T[k].xrN, T[k].yrN, &T[k] ) ) estIllegal = 1;
        }

        if ( estIllegal ) {
            // we drop illegal moves by swapping in the last element (order doesn't matter here)
            T[k] = T[(*n)-1];
            (*n)--;
            k--; // we re-check this slot since it now holds a different move
        }
    }

} // fin de generer_successeurs

/* generates in T the configurations obtained from conf when a pawn at (a,b)
   reaches the far rank at (x,y) */
void transformer_pion( struct config *conf, int a, int b, int x, int y, struct config T[], int *n )
{
        int signe = +1;
        if (conf->mat[a][b] < 0 ) signe = -1;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 'n';        // promote to queen
        (*n)++;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 'c';        // promote to knight
        (*n)++;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 'f';        // promote to bishop
        (*n)++;
        copier(conf, &T[*n]);
        T[*n].mat[a][b] = 0;
        T[*n].mat[x][y] = signe * 't';        // promote to rook
        (*n)++;

} // fin de transformer_pion


/* generates in T all possible moves for the black piece at (x,y) */
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
                   } // while
                } // for
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
                   } // while
                } // for
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
                   } // while
                } // for
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
                } // for
                break;

        }

} // fin de deplacements_noirs


/* generates in T all possible moves for the white piece at (x,y) */
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
                   } // while
                } // for
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
                   } // while
                } // for
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
                   } // while
                } // for
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
                } // for
                break;

        }

} // fin de deplacements_blancs


// utility functions


// checks whether (x,y) is attacked by a piece belonging to player 'mode'
int case_menacee_par( int mode, int x, int y, struct config *conf )
{
        int i, a, b, stop;

        // threatened by the king
        for (i=0; i<8; i += 1) {
           a = x + D[i][0];
           b = y + D[i][1];
           if ( a >= 0 && a <= 7 && b >= 0 && b <= 7 )
                if ( conf->mat[a][b]*mode == 'r' ) return 1;
        } // for

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
        } // for

        return 0;

} // fin de case_menacee_par


/* comparison functions used with qsort */
// 'a' and 'b' are configs
int comparer_config_123(const void *a, const void *b)        // ascending order
{
    int x = ((struct config *)a)->val, y = ((struct config *)b)->val;
    if ( x < y )
        return -1;
    if ( x == y )
        return 0;
    return 1;
}  // fin comparer_config_123

int comparer_config_321(const void *a, const void *b)        // descending order
{
    int x = ((struct config *)a)->val, y = ((struct config *)b)->val;
    if ( x < y )
        return 1;
    if ( x == y )
        return 0;
    return -1;
}  // fin comparer_config_321


/* number of pieces belonging to N and B on the board 'conf' */
int nombre_pieces( struct config *conf )
{
    int i,j;
    int nb = 0;
    for (i=0; i<8; i++)
        for (j=0; j<8; j++)
            if ( conf->mat[i][j] != 0 ) nb++;
    return nb;
}


/* saves conf to file f (for history) */
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

} // fin sauvegarder_configuration


/* builds a text description of the last move played (for display) */
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
} // fin de formuler_coup


/* prints the config 'conf' */
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

} // fin de  afficher_configuration



/* tests whether boards c1 and c2 are equal */
int configurations_egales(char c1[8][8], char c2[8][8] )
{
        int i, j;

        for (i=0; i<8; i++)
                for (j=0; j<8; j++)
                        if (c1[i][j] != c2[i][j]) return 0;
        return 1;
} // fin de configurations_egales


/* ==========================================================================
   UPDATED UTILITIES
   ========================================================================== */

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
   // we could limit the search to the last X moves for performance
   int limit = PartieLen;

   for (int i = 0; i < limit; i++) {
       if (PartieMode[i] != mode) continue;
       // we compare the 64-bit hashes first, since it's fast
       if ( conf->hash == Partie[i].hash ) {
           // on a hash collision (rare) we fall back to comparing the full board
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

    // we also copy the hash; generer_successeurs then edits c2->mat, so it must
    // recompute c2's hash itself after making those changes
    c2->hash = c1->hash;
}


/* tests whether there is no possible move in config 'conf' */
int aucun_coup_possible( struct config *conf, int mode )
{
        struct config T[MAX_MOVES];
        int n = 0;
        // if there are no legal successors, the player to move is stuck
        generer_successeurs(conf, mode, T, &n);
        return n == 0;

} // fin de aucun_coup_possible
