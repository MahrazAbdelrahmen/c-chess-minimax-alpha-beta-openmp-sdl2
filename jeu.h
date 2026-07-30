#ifndef JEU_H
#define JEU_H

#include <limits.h>
#include <omp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX +1
#define MIN -1

#define INFINI INT_MAX
#define MAXPARTIE 512
#define MAX_MOVES 256

// transposition table flags
typedef enum {
    TT_EXACT,
    TT_ALPHA, // upper bound
    TT_BETA   // lower bound
} TTFlag;

// we store one entry per visited position so threads can share results instead of recomputing them
struct TTEntry {
    uint64_t key;
    int value;
    int depth;
    TTFlag flag;
    int bestMoveIndex;
};

struct config {
    char mat[8][8];
    int val;
    char xrN, yrN, xrB, yrB;
    char roqueN, roqueB;

    // we use this zobrist hash for O(1) lookups instead of scanning the board to check if a position was visited
    uint64_t hash;
};

/* --- transposition table / zobrist --- */

void initialiser_zobrist();
uint64_t calculer_hash(struct config* conf);
uint64_t mettre_a_jour_hash(uint64_t hash_courant, struct config* ancienne_conf,
    struct config* nouvelle_conf);
void tt_sauvegarder(uint64_t key, int val, TTFlag flag, int depth, int bestMoveIndex);
bool tt_verifier(uint64_t key, int depth, int alpha, int beta, int* val,
    int* bestMoveIndex);
void tt_liberer();

/* --- search --- */

// we split the work at the root: generate every move, then hand them out across threads that each run alpha-beta sequentially and share the transposition table
int minmax_parallele(struct config* conf, int mode, int niv, int alpha, int beta,
    int largeur, int numFctEst, int npp);

// we run this on each thread after it gets its root move; it checks the transposition table first and saves into it before returning
int minmax_alpha_beta(struct config* conf, int mode, int niv, int min, int max,
    int largeur, int numFctEst, int npp);

/* --- move generation / evaluation --- */

int estimation_1(struct config* conf);
int estimation_2(struct config* conf);
int estimation_3(struct config* conf);
int estimation_4(struct config* conf);
int estimation_5(struct config* conf);
int estimation_6(struct config* conf);
int estimation_7(struct config* conf);

void generer_successeurs(struct config* conf, int mode, struct config T[], int* n);
void transformer_pion(struct config* conf, int a, int b, int x, int y,
    struct config T[], int* n);
void deplacements_noirs(struct config* conf, int x, int y, struct config T[],
    int* n);
void deplacements_blancs(struct config* conf, int x, int y, struct config T[],
    int* n);
int case_menacee_par(int mode, int x, int y, struct config* conf);
void initialiser_configuration(struct config* conf);
int nombre_pieces(struct config* conf);
void afficher_configuration(struct config* conf, char* coup, int num);

// we can use conf->hash here instead of comparing boards cell by cell
int deja_visitee(struct config* conf, int mode);

void sauvegarder_configuration(struct config* conf);
void copier(struct config* c1, struct config* c2);
int configurations_egales(char c1[8][8], char c2[8][8]);
int aucun_coup_possible(struct config* conf, int mode);
int est_feuille(struct config* conf, int mode, int* cout);

int comparer_config_123(const void* a, const void* b);
int comparer_config_321(const void* a, const void* b);
void formuler_coup(struct config* oldconf, struct config* newconf, char* coup);

#endif
