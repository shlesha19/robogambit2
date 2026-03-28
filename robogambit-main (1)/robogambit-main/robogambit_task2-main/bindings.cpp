#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <string>
#include <vector>
#include "bitboard_eval.h" 
#include "move_generation.h"
#include "minimax.h"

namespace py = pybind11;

// Helper to convert 0-35 index to "A1", "F6", etc.
std::string index_to_cell(int sq) {
    char col = 'A' + (sq % 6);
    char row = '1' + (sq / 6);
    return std::string(1, col) + std::string(1, row);
}

std::string find_move_cpp(py::array_t<int> numpy_board, bool is_white) {
    BitBoardState state = {};
    auto board = numpy_board.unchecked<2>(); 

    // 1. Translate 6x6 NumPy array to Bitboards
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 6; col++) {
            int piece_id = board(row, col);
            if (piece_id == 0) continue; 
            
            int idx = row * 6 + col; 
            uint64_t bit = (1ULL << idx);

            switch(piece_id) {
                case 1: state.w_pawns |= bit; state.w_occ |= bit; break;
                case 2: state.w_knights |= bit; state.w_occ |= bit; break;
                case 3: state.w_bishops |= bit; state.w_occ |= bit; break;
                case 4: state.w_queen |= bit; state.w_occ |= bit; break;
                case 5: state.w_king |= bit; state.w_occ |= bit; break;
                case 6: state.b_pawns |= bit; state.b_occ |= bit; break;
                case 7: state.b_knights |= bit; state.b_occ |= bit; break;
                case 8: state.b_bishops |= bit; state.b_occ |= bit; break;
                case 9: state.b_queen |= bit; state.b_occ |= bit; break;
                case 10: state.b_king |= bit; state.b_occ |= bit; break;
            }
        }
    }
    
    // Apply board mask to seal the 36-bit universe
    const uint64_t BOARD_MASK = 0xFFFFFFFFFULL;
    state.empty = ~(state.w_occ | state.b_occ) & BOARD_MASK;

    // 2. Call your root Minimax/Alpha-Beta search
    int search_depth = 8; // Adjust based on your time limit
    int side = is_white ? 1 : 0;
    
    // NOTE: You must ensure get_best_move is declared in move_generation.h 
    // and returns a uint16_t encoded move!
    uint16_t best_move = get_best_move(state, search_depth, side);

    // If no valid moves are returned (e.g., checkmate/stalemate)
    if (best_move == 0) {
        return "None"; 
    }

    // 3. Unpack the move for Python
    int src = best_move & 0x3F;
    int dst = (best_move >> 6) & 0x3F;
    int flag = (best_move >> 12) & 0xF;
    
    // Extract the piece_id from the original NumPy board to satisfy the string format
    int src_row = src / 6;
    int src_col = src % 6;
    int piece_id = board(src_row, src_col);

    // Format: "<piece_id>:<source_cell>-><target_cell>"
    // For promotions, append "=<promoted_piece_id>"
    std::string formatted_move = std::to_string(piece_id) + ":" + index_to_cell(src) + "->" + index_to_cell(dst);
    
    if (flag >= 2 && flag <= 7) {
        int promo_piece;
        if (flag == 2 || flag == 5)      promo_piece = is_white ? 2 : 7;  // knight
        else if (flag == 3 || flag == 6) promo_piece = is_white ? 3 : 8;  // bishop
        else                             promo_piece = is_white ? 4 : 9;  // queen
        formatted_move += "=" + std::to_string(promo_piece);
    }
    
    return formatted_move;
}

// Debug function: count legal moves available for the current position
int count_legal_moves(py::array_t<int> numpy_board, bool is_white) {
    BitBoardState state = {};
    auto board = numpy_board.unchecked<2>();

    // Build bitboard state from NumPy array
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 6; col++) {
            int piece_id = board(row, col);
            if (piece_id == 0) continue;
            
            int idx = row * 6 + col;
            uint64_t bit = (1ULL << idx);

            switch(piece_id) {
                case 1: state.w_pawns |= bit; state.w_occ |= bit; break;
                case 2: state.w_knights |= bit; state.w_occ |= bit; break;
                case 3: state.w_bishops |= bit; state.w_occ |= bit; break;
                case 4: state.w_queen |= bit; state.w_occ |= bit; break;
                case 5: state.w_king |= bit; state.w_occ |= bit; break;
                case 6: state.b_pawns |= bit; state.b_occ |= bit; break;
                case 7: state.b_knights |= bit; state.b_occ |= bit; break;
                case 8: state.b_bishops |= bit; state.b_occ |= bit; break;
                case 9: state.b_queen |= bit; state.b_occ |= bit; break;
                case 10: state.b_king |= bit; state.b_occ |= bit; break;
            }
        }
    }

    // Apply board mask
    const uint64_t BOARD_MASK = 0xFFFFFFFFFULL;
    state.empty = ~(state.w_occ | state.b_occ) & BOARD_MASK;

    int side = is_white ? 1 : 0;
    std::vector<uint16_t> legal_moves = generate_moves(state, side);
    return (int)legal_moves.size();
}

// PyBind11 Module Definition
PYBIND11_MODULE(robogambit_cpp, m) {
    m.doc() = "RoboGambit C++ Bitboard Engine";
    m.def("get_best_move", &find_move_cpp, "Calculates the best move using C++ bitboards");
    m.def("count_legal_moves", &count_legal_moves, "Returns the number of legal moves for the current position");
}