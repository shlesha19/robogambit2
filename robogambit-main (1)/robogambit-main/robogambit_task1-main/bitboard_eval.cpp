#include "bitboard_eval.h"
#include "move_generation.h"

// ===========================================================================
// PIECE-SQUARE TABLES  (from White's perspective, rank-1 at the bottom)
// Index 0 = A1, 5 = F1, 6 = A2, … 35 = F6
// Black tables are mirrored vertically at lookup time.
// ===========================================================================

static const int pawn_pst[36] = {
     0  , 5   ,5   ,5   ,5  , 0,   // rank 1 (pawns never here)
      10  ,10  ,10  ,10  ,10 ,10,   // rank 2
     20  ,20  ,25  ,25  ,20 ,20,   // rank 3
     35  ,35  ,40  ,40  ,35 ,35,   // rank 4
     60  ,60  ,60  ,60  ,60 ,60,   // rank 5
      0,   0,   0,   0,   0,   0    // rank 6 (promotion rank)
};

static const int knight_pst[36] = {
    -10  , -5   , 0   , 0  , -5 , -10,
    -5  , 15  , 20  , 20  , 15  , -5,
     0  , 20  , 30  , 30  , 20   , 0,
     0  , 20  , 30  , 30  , 20   , 0,
    -5  , 15  , 20  , 20  , 15  , -5,
   -10  , -5   , 0   , 0  , -5 , -10,
};

static const int bishop_pst[36] = {
    -5  , 0   , 5   , 5   , 0  , -5,
    0  , 15  , 15  , 15  , 15   , 0,
    5  , 15  , 20  , 20  , 15   , 5,
    5  , 15  , 20  , 20  , 15   , 5,
    0  , 15  , 15  , 15  , 15   , 0,
   -5   , 0   , 5   , 5   , 0  , -5
};

static const int queen_pst[36] = {
      0  , 10  , 15  , 15  , 10   , 0,   //queen being the priority piece
     10  , 25  , 30  , 30  , 25  , 10,
     15  , 30  , 40  , 40  , 30  , 15,
     15  , 30  , 40  , 40  , 30  , 15,
     10  , 25  , 30  , 30  , 25  , 10,
      0  , 10  , 15  , 15  , 10   , 0
};

static const int king_pst[36] = {
     30  , 20  , 10  , 10  , 20  , 30,    //king plays safely
     20  , 10  , 0   , 0   , 10  , 20,
     10  , 0   , -10 , -10 , 0   , 10,
     10  , 0   , -10 , -10 , 0   , 10,
     20  , 10  , 0   , 0   , 10  , 20,
     30  , 20  , 10  , 10  , 20  , 30
};

// Mirror index vertically for Black: row r -> row (5 - r), same column
static inline int mirror_sq(int sq) {
    int row = sq / 6;
    int col = sq % 6;
    return (5 - row) * 6 + col;
}

// ===========================================================================
// WEIGHTS
// ===========================================================================
static constexpr int W_MOBILITY   = 4;    // w_m
static constexpr int W_PASSED     = 20;   // w_pp
static constexpr int W_DOUBLED    = 10;   // w_dp
static constexpr int W_ISOLATED   = 12;   // w_ip
static constexpr int W_BACKWARD   = 8;    // w_bp
static constexpr int W_SHIELD     = 12;   // w_shield
static constexpr int W_ATTACK     = 8;    // w_attack
static constexpr int W_CENTER     = 10;   // w_c
static constexpr int W_SPACE      = 3;    // w_s

// Material values
static constexpr int VAL_PAWN   = 100;
static constexpr int VAL_KNIGHT = 320;
static constexpr int VAL_BISHOP = 330;
static constexpr int VAL_QUEEN      = 950;
static constexpr int VAL_QUEEN_OPP  = 900;   // enemy queen worth less than ours
static constexpr int VAL_KING       = 200000;

// ===========================================================================
// 1. MATERIAL
// ===========================================================================
int score_material(const BitBoardState& state) {
    int w = VAL_PAWN       * popcount(state.w_pawns)
          + VAL_KNIGHT     * popcount(state.w_knights)
          + VAL_BISHOP     * popcount(state.w_bishops)
          + VAL_QUEEN      * popcount(state.w_queen)
          + VAL_KING       * popcount(state.w_king);

    int b = VAL_PAWN       * popcount(state.b_pawns)
          + VAL_KNIGHT     * popcount(state.b_knights)
          + VAL_BISHOP     * popcount(state.b_bishops)
          + VAL_QUEEN_OPP  * popcount(state.b_queen)
          + VAL_KING       * popcount(state.b_king);

    return w - b;
}

// ===========================================================================
// 2. PIECE-SQUARE TABLES    Score_pst = Σ PST(p,s)[White] − Σ PST(p,s)[Black]
// ===========================================================================
static int pst_sum(uint64_t bb, const int table[36], bool is_black) {
    int total = 0;
    while (bb) {
        int sq = __builtin_ctzll(bb);
        total += table[is_black ? mirror_sq(sq) : sq];
        bb &= bb - 1;
    }
    return total;
}

int score_pst(const BitBoardState& state) {
    int w = pst_sum(state.w_pawns,   pawn_pst,   false)
          + pst_sum(state.w_knights, knight_pst, false)
          + pst_sum(state.w_bishops, bishop_pst, false)
          + pst_sum(state.w_queen,   queen_pst,  false)
          + pst_sum(state.w_king,    king_pst,   false);

    int b = pst_sum(state.b_pawns,   pawn_pst,   true)
          + pst_sum(state.b_knights, knight_pst, true)
          + pst_sum(state.b_bishops, bishop_pst, true)
          + pst_sum(state.b_queen,   queen_pst,  true)
          + pst_sum(state.b_king,    king_pst,   true);

    return w - b;
}

// ===========================================================================
// 3. MOBILITY   Score_mobility = w_m * (Σ M(p)[White] − Σ M(p)[Black])
// Uses the existing generate_moves() from move_generation.h
// ===========================================================================
int score_mobility(const BitBoardState& state) {
    int w_moves = (int)generate_moves(state, 1).size();
    int b_moves = (int)generate_moves(state, 0).size();
    return W_MOBILITY * (w_moves - b_moves);
}

// ===========================================================================
// 4. PAWN STRUCTURE
//    Score_pawn = w_pp·Passed − w_dp·Doubled − w_ip·Isolated − w_bp·Backward
//    All counts are (white_feature − black_feature).
// ===========================================================================

// File masks for the 6-file board (columns 0-5)
static constexpr uint64_t FILE_MASKS[6] = {
    A_FILE,           // file 0 = A
    A_FILE << 1,      // file 1 = B
    A_FILE << 2,      // file 2 = C
    A_FILE << 3,      // file 3 = D
    A_FILE << 4,      // file 4 = E
    A_FILE << 5       // file 5 = F
};

// Adjacent file masks (for isolated / backward detection)
static uint64_t adjacent_files(int f) {
    uint64_t mask = 0;
    if (f > 0) mask |= FILE_MASKS[f - 1];
    if (f < 5) mask |= FILE_MASKS[f + 1];
    return mask;
}

// Ranks ahead masks: all squares strictly in front of sq for given side
// White pawns advance toward higher ranks (higher bits).
// Black pawns advance toward lower ranks (lower bits).
static uint64_t ranks_ahead_white(int sq) {
    // All bits in ranks above sq's rank, same file or full board
    int rank = sq / 6;
    if (rank >= 5) return 0;
    // Shift BOARD_MASK to clear ranks 0..rank
    return BOARD_MASK & ~((1ULL << ((rank + 1) * 6)) - 1);
}

static uint64_t ranks_ahead_black(int sq) {
    int rank = sq / 6;
    if (rank <= 0) return 0;
    return (1ULL << (rank * 6)) - 1;
}

static void count_pawn_features(uint64_t own_pawns, uint64_t enemy_pawns,
                                bool is_white,
                                int& passed, int& doubled, int& isolated, int& backward) {
    passed = doubled = isolated = backward = 0;

    for (int f = 0; f < 6; f++) {
        uint64_t own_on_file = own_pawns & FILE_MASKS[f];
        int cnt = popcount(own_on_file);
        if (cnt == 0) continue;

        // Doubled: more than one pawn on the same file
        if (cnt > 1) doubled += cnt - 1;

        // Isolated: no friendly pawns on adjacent files
        uint64_t adj = adjacent_files(f);
        if (!(own_pawns & adj)) isolated += cnt;

        // Per-pawn checks for passed and backward
        uint64_t pawns_copy = own_on_file;
        while (pawns_copy) {
            int sq = __builtin_ctzll(pawns_copy);
            pawns_copy &= pawns_copy - 1;

            uint64_t ahead = is_white ? ranks_ahead_white(sq) : ranks_ahead_black(sq);

            // Passed: no enemy pawn on same file or adjacent files ahead
            uint64_t block_zone = ahead & (FILE_MASKS[f] | adjacent_files(f));
            if (!(enemy_pawns & block_zone)) passed++;

            // Backward: no friendly pawn on adjacent files at same rank or behind
            uint64_t behind = is_white ? ranks_ahead_black(sq) : ranks_ahead_white(sq);
            int rank = sq / 6;
            uint64_t same_rank = 0x3FULL << (rank * 6);  // the 6 bits of this rank
            uint64_t support_zone = (behind | same_rank) & adj;
            if (!(own_pawns & support_zone)) {
                // Only backward if an enemy pawn blocks the stop square
                int stop = is_white ? sq + 6 : sq - 6;
                if (stop >= 0 && stop < 36) {
                    uint64_t enemy_control = adjacent_files(stop % 6) & (is_white ? ranks_ahead_black(stop) : ranks_ahead_white(stop));
                    // Simplified: if any enemy pawn on adjacent file at or above stop
                    uint64_t stop_rank_mask = is_white ? ranks_ahead_white(sq) : ranks_ahead_black(sq);
                    if (enemy_pawns & adj & stop_rank_mask)
                        backward++;
                }
            }
        }
    }
}

int score_pawn_structure(const BitBoardState& state) {
    int w_passed, w_doubled, w_isolated, w_backward;
    int b_passed, b_doubled, b_isolated, b_backward;

    count_pawn_features(state.w_pawns, state.b_pawns, true,
                        w_passed, w_doubled, w_isolated, w_backward);
    count_pawn_features(state.b_pawns, state.w_pawns, false,
                        b_passed, b_doubled, b_isolated, b_backward);

    int w_score = W_PASSED * w_passed - W_DOUBLED * w_doubled
                - W_ISOLATED * w_isolated - W_BACKWARD * w_backward;
    int b_score = W_PASSED * b_passed - W_DOUBLED * b_doubled
                - W_ISOLATED * b_isolated - W_BACKWARD * b_backward;

    return w_score - b_score;
}

// ===========================================================================
// 5. KING SAFETY   Score_king = w_shield · PawnShield − w_attack · EnemyAttack
// ===========================================================================

// Pawn shield: count friendly pawns on the three squares directly in front
// of the king (and one rank further if on back rank).
static int pawn_shield(uint64_t king_bb, uint64_t own_pawns, bool is_white) {
    if (!king_bb) return 0;
    int ksq = __builtin_ctzll(king_bb);
    int kfile = ksq % 6;
    int krank = ksq / 6;
    int shield = 0;

    // Shield rank: the rank directly in front of the king
    int front_rank = is_white ? krank + 1 : krank - 1;
    if (front_rank < 0 || front_rank > 5) return 0;

    for (int f = kfile - 1; f <= kfile + 1; f++) {
        if (f < 0 || f > 5) continue;
        int sq = front_rank * 6 + f;
        if (own_pawns & (1ULL << sq)) shield++;
        // Also check two ranks ahead
        int sq2 = (is_white ? front_rank + 1 : front_rank - 1) * 6 + f;
        if (sq2 >= 0 && sq2 < 36 && (own_pawns & (1ULL << sq2))) shield++;
    }
    return shield;
}

// Enemy attack around king: count enemy pieces/pawns attacking the king zone
static int enemy_attacks_near_king(uint64_t king_bb, const BitBoardState& state, bool is_white) {
    if (!king_bb) return 0;
    int ksq = __builtin_ctzll(king_bb);
    uint64_t king_bit = 1ULL << ksq;

    // King zone: the 8 squares around the king (+ king square itself)
    uint64_t zone = 0;
    zone |= ((king_bit & NOT_A_FILE) << 5);
    zone |= (king_bit << 6);
    zone |= ((king_bit & NOT_F_FILE) << 7);
    zone |= ((king_bit & NOT_A_FILE) >> 1);
    zone |= ((king_bit & NOT_F_FILE) << 1);
    zone |= ((king_bit & NOT_A_FILE) >> 7);
    zone |= (king_bit >> 6);
    zone |= ((king_bit & NOT_F_FILE) >> 5);
    zone &= BOARD_MASK;

    // Count enemy pieces that are in or attack the king zone
    uint64_t enemy_occ = is_white ? state.b_occ : state.w_occ;
    return popcount(zone & enemy_occ);
}

int score_king_safety(const BitBoardState& state) {
    int w_shield = pawn_shield(state.w_king, state.w_pawns, true);
    int w_enemy  = enemy_attacks_near_king(state.w_king, state, true);
    int w_score  = W_SHIELD * w_shield - W_ATTACK * w_enemy;

    int b_shield = pawn_shield(state.b_king, state.b_pawns, false);
    int b_enemy  = enemy_attacks_near_king(state.b_king, state, false);
    int b_score  = W_SHIELD * b_shield - W_ATTACK * b_enemy;

    return w_score - b_score;
}

// ===========================================================================
// 6. CENTER CONTROL   Score_center = w_c * (C_W − C_B)
//    Center squares on a 6x6 board: C3(14), D3(15), C4(20), D4(21)
// ===========================================================================
static constexpr uint64_t CENTER_MASK = (1ULL << 14) | (1ULL << 15) |
                                        (1ULL << 20) | (1ULL << 21);

int score_center_control(const BitBoardState& state) {
    int c_w = popcount(state.w_occ & CENTER_MASK);
    int c_b = popcount(state.b_occ & CENTER_MASK);
    return W_CENTER * (c_w - c_b);
}

// ===========================================================================
// 7. SPACE / BOARD CONTROL   Score_space = w_s * (S_W − S_B)
//    S = number of squares controlled (occupied or attacked).
//    Approximation: count squares each side occupies + pawn attack squares.
// ===========================================================================
static uint64_t white_pawn_attacks(uint64_t pawns) {
    uint64_t left  = (pawns & NOT_A_FILE) << 5;
    uint64_t right = (pawns & NOT_F_FILE) << 7;
    return (left | right) & BOARD_MASK;
}

static uint64_t black_pawn_attacks(uint64_t pawns) {
    uint64_t left  = (pawns & NOT_A_FILE) >> 7;
    uint64_t right = (pawns & NOT_F_FILE) >> 5;
    return (left | right) & BOARD_MASK;
}

int score_space(const BitBoardState& state) {
    // Space = squares occupied + squares attacked by pawns (non-overlapping)
    uint64_t w_space = state.w_occ | white_pawn_attacks(state.w_pawns);
    uint64_t b_space = state.b_occ | black_pawn_attacks(state.b_pawns);
    int s_w = popcount(w_space);
    int s_b = popcount(b_space);
    return W_SPACE * (s_w - s_b);
}

// ===========================================================================
// 8. TOTAL EVALUATION
//    Eval = Material + PST + Mobility + Pawn + King + Center + Space
// ===========================================================================
int evaluate(const BitBoardState& state) {
    int score = 0;
    score += score_material(state);
    score += score_pst(state);
    score += score_mobility(state);
    score += score_pawn_structure(state);
    score += score_king_safety(state);
    score += score_center_control(state);
    score += score_space(state);
    return score;
}