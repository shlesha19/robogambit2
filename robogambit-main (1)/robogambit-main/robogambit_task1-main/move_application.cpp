#include "move_application.h"

// ---------------------------------------------------------------------------
// 3. MOVE APPLICATION
// ---------------------------------------------------------------------------

//PAWN PROMOTION IS HANDLED VIA FLAGS 2-7
void apply_move (BitBoardState& state, uint16_t move, int side) {
    int src = get_src(move);
    int dst = get_dst(move);
    int flag = get_flag(move);
    
    uint64_t src_mask = (1ULL << src);
    uint64_t dst_mask = (1ULL << dst);
    uint64_t toggle_mask = src_mask | dst_mask; // Has 1s at both src and dst

    if (side == 1) { // White is moving
        if (flag >= 2 && flag <= 7) {
            // --- PAWN PROMOTION ---
            // Remove the pawn from source
            state.w_pawns &= ~src_mask;
            // Place promoted piece at destination
            if (flag == 2 || flag == 5)      state.w_knights |= dst_mask;
            else if (flag == 3 || flag == 6) state.w_bishops |= dst_mask;
            else                             state.w_queen  |= dst_mask;
            // Update occupancy: remove src, add dst
            state.w_occ = (state.w_occ & ~src_mask) | dst_mask;
            // Handle capture (flags 5, 6, 7)
            if (flag >= 5 && (state.b_occ & dst_mask)) {
                state.b_occ &= ~dst_mask;
                if(state.b_knights & dst_mask)      state.b_knights &= ~dst_mask;
                else if(state.b_pawns & dst_mask)   state.b_pawns   &= ~dst_mask;
                else if(state.b_bishops & dst_mask) state.b_bishops &= ~dst_mask;
                else if(state.b_king & dst_mask)    state.b_king    &= ~dst_mask;
                else                                state.b_queen   &= ~dst_mask;
            }
        } else {
            // --- NORMAL MOVE ---
            if(state.w_knights & src_mask)
            state.w_knights ^= toggle_mask;
            else if(state.w_pawns & src_mask)
            state.w_pawns ^= toggle_mask;
            else if(state.w_bishops & src_mask)
            state.w_bishops ^= toggle_mask;
            else if(state.w_king & src_mask)
            state.w_king ^= toggle_mask;
            else
            state.w_queen ^= toggle_mask;

            state.w_occ ^= toggle_mask;
            
            // Handle Captures
            if (state.b_occ & dst_mask) {
                state.b_occ &= ~dst_mask;
                if(state.b_knights & dst_mask)      state.b_knights &= ~dst_mask;
                else if(state.b_pawns & dst_mask)   state.b_pawns   &= ~dst_mask;
                else if(state.b_bishops & dst_mask) state.b_bishops &= ~dst_mask;
                else if(state.b_king & dst_mask)    state.b_king    &= ~dst_mask;
                else                                state.b_queen   &= ~dst_mask;
            }
        }
    } else {
        if (flag >= 2 && flag <= 7) {
            // --- PAWN PROMOTION ---
            state.b_pawns &= ~src_mask;
            if (flag == 2 || flag == 5)      state.b_knights |= dst_mask;
            else if (flag == 3 || flag == 6) state.b_bishops |= dst_mask;
            else                             state.b_queen  |= dst_mask;
            state.b_occ = (state.b_occ & ~src_mask) | dst_mask;
            if (flag >= 5 && (state.w_occ & dst_mask)) {
                state.w_occ &= ~dst_mask;
                if(state.w_knights & dst_mask)      state.w_knights &= ~dst_mask;
                else if(state.w_pawns & dst_mask)   state.w_pawns   &= ~dst_mask;
                else if(state.w_bishops & dst_mask) state.w_bishops &= ~dst_mask;
                else if(state.w_king & dst_mask)    state.w_king    &= ~dst_mask;
                else                                state.w_queen   &= ~dst_mask;
            }
        } else {
            // --- NORMAL MOVE ---
            if(state.b_knights & src_mask)
            state.b_knights ^= toggle_mask;
            else if(state.b_pawns & src_mask)
            state.b_pawns ^= toggle_mask;
            else if(state.b_bishops & src_mask)
            state.b_bishops ^= toggle_mask;
            else if(state.b_king & src_mask)
            state.b_king ^= toggle_mask;
            else
            state.b_queen ^= toggle_mask;

            state.b_occ ^= toggle_mask;
            
            // Handle Captures
            if (state.w_occ & dst_mask) {
                state.w_occ &= ~dst_mask;
                if(state.w_knights & dst_mask)      state.w_knights &= ~dst_mask;
                else if(state.w_pawns & dst_mask)   state.w_pawns   &= ~dst_mask;
                else if(state.w_bishops & dst_mask) state.w_bishops &= ~dst_mask;
                else if(state.w_king & dst_mask)    state.w_king    &= ~dst_mask;
                else                                state.w_queen   &= ~dst_mask;
            }
        }
    }
    // 36 bits of 1s. Masks out the phantom bits 36-63.

    // Update global empty squares
    state.empty = ~(state.w_occ | state.b_occ) & BOARD_MASK;
}