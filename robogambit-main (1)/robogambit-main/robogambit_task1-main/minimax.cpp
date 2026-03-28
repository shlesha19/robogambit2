// ---------------------------------------------------------------------------
// 5. MINIMAX SEARCH
// ---------------------------------------------------------------------------

#include "minimax.h"
#include "bitboard_eval.h"
#include "move_application.h"
#include "move_generation.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// MOVE ORDERING — score each move for sort priority (higher = searched first)
// Priority: promotion > capture > mobility > center > material, king deprioritized
// ---------------------------------------------------------------------------

// Center squares on the 6×6 board (C3, D3, C4, D4 = indices 14, 15, 20, 21)
// Extended center includes B3, E3, B4, E4 (13, 16, 19, 22)
static int center_bonus(int sq) {
    int row = sq / 6;
    int col = sq % 6;
    // Inner center (rows 2-3, cols 2-3)
    if (row >= 2 && row <= 3 && col >= 2 && col <= 3) return 200;
    // Extended center (rows 2-3, cols 1-4)
    if (row >= 2 && row <= 3 && col >= 1 && col <= 4) return 100;
    // Rows 1-4, cols 1-4
    if (row >= 1 && row <= 4 && col >= 1 && col <= 4) return 50;
    return 0;
}

// Determine victim value for MVV-LVA capture scoring
static int victim_value(const BitBoardState& state, int dst, int side) {
    uint64_t dst_mask = (1ULL << dst);
    // Identify enemy piece at dst
    if (side == 1) { // White moving, victim is black
        if (state.b_queen  & dst_mask) return 900;
        if (state.b_knights & dst_mask) return 320;
        if (state.b_bishops & dst_mask) return 330;
        if (state.b_pawns  & dst_mask) return 100;
    } else {
        if (state.w_queen  & dst_mask) return 900;
        if (state.w_knights & dst_mask) return 320;
        if (state.w_bishops & dst_mask) return 330;
        if (state.w_pawns  & dst_mask) return 100;
    }
    return 0;
}

// Determine attacker value for MVV-LVA (lower attacker = better trade)
static int attacker_value(const BitBoardState& state, int src, int side) {
    uint64_t src_mask = (1ULL << src);
    if (side == 1) {
        if (state.w_pawns   & src_mask) return 100;
        if (state.w_knights & src_mask) return 320;
        if (state.w_bishops & src_mask) return 330;
        if (state.w_queen   & src_mask) return 900;
        if (state.w_king    & src_mask) return 20000;
    } else {
        if (state.b_pawns   & src_mask) return 100;
        if (state.b_knights & src_mask) return 320;
        if (state.b_bishops & src_mask) return 330;
        if (state.b_queen   & src_mask) return 900;
        if (state.b_king    & src_mask) return 20000;
    }
    return 0;
}

// Check if the moving piece is a king
static bool is_king_move(const BitBoardState& state, int src, int side) {
    uint64_t src_mask = (1ULL << src);
    return (side == 1) ? (state.w_king & src_mask) : (state.b_king & src_mask);
}

// Count how many squares the piece at dst can attack after the move (mobility estimate)
static int mobility_after_move(const BitBoardState& state, uint16_t move, int side) {
    BitBoardState next = state;
    apply_move(next, move, side);
    // Count total legal-ish mobility for the moving side after this move
    // Use a lightweight proxy: number of pseudo-legal moves for that side
    // To keep it fast, we count generate_moves on the resulting state
    // but that's expensive — instead, use a simpler heuristic:
    // pieces that move to central/open positions get a mobility bonus
    int dst = get_dst(move);
    int row = dst / 6;
    int col = dst % 6;
    int bonus = 0;
    // Reward moves away from edges (more mobility potential)
    if (col > 0 && col < 5) bonus += 15;
    if (row > 0 && row < 5) bonus += 15;
    // Extra for inner squares
    if (col >= 2 && col <= 3) bonus += 10;
    if (row >= 2 && row <= 3) bonus += 10;
    return bonus;
}

static int score_move(const BitBoardState& state, uint16_t move, int side) {
    int score = 0;
    int flag = get_flag(move);
    int src = get_src(move);
    int dst = get_dst(move);

    // 1. Promotions — highest priority (10000+)
    if (flag >= 2 && flag <= 7) {
        score += 10000;
        // Capture-promotions even higher
        if (flag >= 5) score += 5000 + victim_value(state, dst, side);
        // Queen promo > bishop > knight
        if (flag == 4 || flag == 7) score += 900;
        else if (flag == 3 || flag == 6) score += 330;
        else score += 320;
        return score;
    }

    // 2. Captures — scored by MVV-LVA (5000+)
    if (flag == 1) {
        int vv = victim_value(state, dst, side);
        int av = attacker_value(state, src, side);
        // MVV-LVA: high victim value, low attacker value = best capture
        score += 5000 + (vv * 10) - av;
    }

    // 3. King move penalty (unless it's a capture already scored above)
    if (is_king_move(state, src, side)) {
        if (flag != 1) {
            score -= 500; // Deprioritize quiet king moves
        }
    }

    // 4. Mobility heuristic
    score += mobility_after_move(state, move, side);

    // 5. Central occupancy bonus for destination
    score += center_bonus(dst);

    // 6. Material positional value (minor tiebreaker via PST-like center gravity)
    // Reward moving non-pawn pieces toward center proportionally to their value
    int av = attacker_value(state, src, side);
    if (av > 100 && av < 20000) { // Non-pawn, non-king pieces
        score += center_bonus(dst) / 10;
    }

    return score;
}

static void order_moves(std::vector<uint16_t>& moves, const BitBoardState& state, int side) {
    std::sort(moves.begin(), moves.end(), [&](uint16_t a, uint16_t b) {
        return score_move(state, a, side) > score_move(state, b, side);
    });
}

// ---------------------------------------------------------------------------
// 5. MINIMAX SEARCH WITH ALPHA-BETA
// ---------------------------------------------------------------------------

int V(BitBoardState state, int depth, int alpha, int beta, int side) {
    if (depth == 0) {
        return evaluate(state); // Static evaluation at leaf nodes 
    }

    std::vector<uint16_t> moves = generate_moves(state, side);
    
    // Terminal State Handling
    if (moves.empty()) {
        if (is_in_check(state, side)) {
            // Checkmate: Returning values relative to depth encourages the AI
            // to find the fastest mate or delay being mated.
            if (side == 1) {
                return -200000 - depth;
            } else {
                return 200000 + depth;
            }
        }
        return 0; // Stalemate
    }

    // Move ordering for better alpha-beta pruning
    order_moves(moves, state, side);

    if (side == 1) { // Maximizing Player (White)
        int max_eval = -999999;
        for (uint16_t move : moves) {
            BitBoardState next_state = state; 
            apply_move(next_state, move, side);

            // 'eval' here is the dynamic value from future boards 
            int eval = V(next_state, depth - 1, alpha, beta, 0);
            max_eval = std::max(max_eval, eval);
            alpha = std::max(alpha, eval);
            if (beta <= alpha) {
                break; // Beta cut-off
            }
        }
        return max_eval;
    } 
    else { // Minimizing Player (Black)
        int min_eval = 999999;
        for (uint16_t move : moves) {
            BitBoardState next_state = state;
            apply_move(next_state, move, side);

            int eval = V(next_state, depth - 1, alpha, beta, 1);
            min_eval = std::min(min_eval, eval);
            beta = std::min(beta, eval);
            if (beta <= alpha) {
                break; // Alpha cut-off
            }
        }
        return min_eval;
    }
}

// ---------------------------------------------------------------------------
// 6. ROOT CALLER
// ---------------------------------------------------------------------------

uint16_t get_best_move(BitBoardState state, int depth, int side) {
    std::vector<uint16_t> moves = generate_moves(state, side);
    if (moves.empty()) {
        return 0; 
    }

    // Move ordering for better alpha-beta pruning
    order_moves(moves, state, side);

    uint16_t best_move = moves[0];
    int alpha = -999999;
    int beta = 999999;
    int best_score;

    if (side == 1) {
        best_score = -999999;
    } else {
        best_score = 999999;
    }

    for (uint16_t move : moves) {
        BitBoardState next_state = state;
        apply_move(next_state, move, side);
        
        int eval = V(next_state, depth - 1, alpha, beta, 1 - side);

        if (side == 1) { // White (Maximizer)
            if (eval > best_score) {
                best_score = eval;
                best_move = move;
            }
            alpha = std::max(alpha, best_score);
        } else { // Black (Minimizer)
            if (eval < best_score) {
                best_score = eval;
                best_move = move;
            }
            beta = std::min(beta, best_score);
        }
    }
    return best_move;
}