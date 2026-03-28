// ---------------------------------------------------------------------------
// 4. MOVE GENERATION
// ---------------------------------------------------------------------------
#include "move_generation.h"

bool is_in_check(const BitBoardState& state, int side) {
    uint64_t king_bb;
    if (side == 1) {
        king_bb = state.w_king;
    } else {
        king_bb = state.b_king;
    }
    
    if (king_bb == 0) return false;

    uint64_t blockers = state.w_occ | state.b_occ;
    uint64_t enemy_knights;
    uint64_t enemy_pawns;
    uint64_t enemy_sliders;
    uint64_t enemy_bishops;
    uint64_t enemy_queen;

    if (side == 1) { // Checking Black attackers
        enemy_knights = state.b_knights;
        enemy_pawns = state.b_pawns;
        enemy_sliders = state.b_bishops | state.b_queen;
        enemy_bishops = state.b_bishops;
        enemy_queen = state.b_queen;
    } else { // Checking White attackers
        enemy_knights = state.w_knights;
        enemy_pawns = state.w_pawns;
        enemy_sliders = state.w_bishops | state.w_queen;
        enemy_bishops = state.w_bishops;
        enemy_queen = state.w_queen;
    }

    // 1. Knight Attacks
    uint64_t knight_map = 0;
    uint64_t k = king_bb;
    knight_map |= ((k & NOT_A_FILE)  << 11);   
    knight_map |= ((k & NOT_F_FILE)  << 13);
    knight_map |= ((k & NOT_AB_FILE) << 4);
    knight_map |= ((k & NOT_EF_FILE) << 8);
    knight_map |= ((k & NOT_F_FILE)  >> 11);
    knight_map |= ((k & NOT_A_FILE)  >> 13);
    knight_map |= ((k & NOT_EF_FILE) >> 4);
    knight_map |= ((k & NOT_AB_FILE) >> 8);
    if (knight_map & enemy_knights & BOARD_MASK) return true;

    // 2. Pawn Attacks
    if (side == 1) { // White King vs Black Pawns (Black pawns attack downward)
        if ((((king_bb & NOT_A_FILE) << 5) & enemy_pawns) | (((king_bb & NOT_F_FILE) << 7) & enemy_pawns)) return true;
    } else { // Black King vs White Pawns (White pawns attack upward)
        if ((((king_bb & NOT_A_FILE) >> 7) & enemy_pawns) | (((king_bb & NOT_F_FILE) >> 5) & enemy_pawns)) return true;
    }

    // 3. Sliding Attacks
    int dirs[] = {5, 7, -5, -7, 6, -6, 1, -1};
    for (int i = 0; i < 8; i++) {
        int d = dirs[i];
        uint64_t ray = king_bb;
        while (true) {
            // Apply file masks BEFORE shifts to prevent edge wrapping
            if (d == 5) {       // Up-Left: apply NOT_A_FILE
                ray = (ray & NOT_A_FILE) << 5;
            } else if (d == 7) { // Up-Right: apply NOT_F_FILE
                ray = (ray & NOT_F_FILE) << 7;
            } else if (d == -5) { // Down-Right: apply NOT_F_FILE
                ray = (ray & NOT_F_FILE) >> 5;
            } else if (d == -7) { // Down-Left: apply NOT_A_FILE
                ray = (ray & NOT_A_FILE) >> 7;
            } else if (d == 6) {  // Up: no mask needed
                ray = ray << 6;
            } else if (d == -6) { // Down: no mask needed
                ray = ray >> 6;
            } else if (d == 1) {  // Right: apply NOT_F_FILE
                ray = (ray & NOT_F_FILE) << 1;
            } else if (d == -1) { // Left: apply NOT_A_FILE
                ray = (ray & NOT_A_FILE) >> 1;
            }
            
            // Enforce 36-bit board boundary
            ray = ray & BOARD_MASK;
            if (ray == 0) break;
            
            // Check for enemy sliders BEFORE the blocker break so they are detected
            if (ray & enemy_queen) return true;
            if ((d == 7 || d == 5 || d == -7 || d == -5) && (ray & enemy_bishops)) return true;

            // Check for blockers: stop ray if we hit any piece (friendly or enemy)
            if (ray & blockers) break;
        }
    }
    return false;
}

std::vector<uint16_t> generate_moves(const BitBoardState& state, int side) {
    std::vector<uint16_t> moves;
    moves.reserve(40); // Pre-allocate to save memory reallocation time

    if (side == 1) { // White
        // --- PAWN PUSHES (excluding promotions) ---
        // Shift pawns UP by 6, mask with empty squares, exclude promotion rank
        uint64_t single_pushes = (state.w_pawns << 6) & state.empty & ~RANK_6;
        
        uint64_t pushes_copy = single_pushes;
        while (pushes_copy) {
            int dst = __builtin_ctzll(pushes_copy);
            int src = dst - 6; // Reverse the shift to find the origin
            moves.push_back(encode_move(src, dst));
            pushes_copy &= (pushes_copy - 1); //this is NOT address wala &, it is boolean, this erases the lowermost set bit in a vector. 
        }

        // --- PAWN CAPTURES (excluding promotions) ---
        uint64_t top_left_captures = ((state.w_pawns & NOT_A_FILE)<<5) & state.b_occ & ~RANK_6;
        uint64_t tl_captures_copy = top_left_captures;
        while (tl_captures_copy) {
            int dst = __builtin_ctzll(tl_captures_copy);
            int src = dst - 5; // Reverse the shift to find the origin
            moves.push_back(encode_move(src, dst, 1)); //1 for capture
            tl_captures_copy &= (tl_captures_copy - 1); //this is NOT address wala &, it is boolean, this erases the lowermost set bit in a vector. 
        }
        uint64_t top_right_captures = ((state.w_pawns & NOT_F_FILE)<<7) & state.b_occ & ~RANK_6;
        uint64_t tr_captures_copy = top_right_captures;
        while (tr_captures_copy) {
            int dst = __builtin_ctzll(tr_captures_copy);
            int src = dst - 7; // Reverse the shift to find the origin
            moves.push_back(encode_move(src, dst, 1));
            tr_captures_copy &= (tr_captures_copy - 1); //this is NOT address wala &, it is boolean, this erases the lowermost set bit in a vector. 
        }

        // --- PAWN PROMOTIONS ---
        // A pawn promotes when it reaches rank 6 (bits 30-35).
        // It can only promote to a piece type whose count is below the starting count.
        bool can_promo_knight = popcount(state.w_knights) < 2;
        bool can_promo_bishop = popcount(state.w_bishops) < 2;
        bool can_promo_queen  = state.w_queen == 0;

        if (can_promo_knight || can_promo_bishop || can_promo_queen) {
            // Promotion pushes
            uint64_t promo_pushes = (state.w_pawns << 6) & state.empty & RANK_6;
            while (promo_pushes) {
                int dst = __builtin_ctzll(promo_pushes);
                int src = dst - 6;
                if (can_promo_knight) moves.push_back(encode_move(src, dst, 2));
                if (can_promo_bishop) moves.push_back(encode_move(src, dst, 3));
                if (can_promo_queen)  moves.push_back(encode_move(src, dst, 4));
                promo_pushes &= (promo_pushes - 1);
            }
            // Promotion captures (top-left)
            uint64_t promo_tl = ((state.w_pawns & NOT_A_FILE) << 5) & state.b_occ & RANK_6;
            while (promo_tl) {
                int dst = __builtin_ctzll(promo_tl);
                int src = dst - 5;
                if (can_promo_knight) moves.push_back(encode_move(src, dst, 5));
                if (can_promo_bishop) moves.push_back(encode_move(src, dst, 6));
                if (can_promo_queen)  moves.push_back(encode_move(src, dst, 7));
                promo_tl &= (promo_tl - 1);
            }
            // Promotion captures (top-right)
            uint64_t promo_tr = ((state.w_pawns & NOT_F_FILE) << 7) & state.b_occ & RANK_6;
            while (promo_tr) {
                int dst = __builtin_ctzll(promo_tr);
                int src = dst - 7;
                if (can_promo_knight) moves.push_back(encode_move(src, dst, 5));
                if (can_promo_bishop) moves.push_back(encode_move(src, dst, 6));
                if (can_promo_queen)  moves.push_back(encode_move(src, dst, 7));
                promo_tr &= (promo_tr - 1);
            }
        }

        // --- KNIGHT MOVES ---
        uint64_t knights_copy = state.w_knights;
        uint64_t valid_squares = ~state.w_occ & BOARD_MASK; // Phantom bounds sealed

        // Iterate through each knight one by one
        while (knights_copy) {
            // 1. Isolate the current knight's source index
            int src = __builtin_ctzll(knights_copy);
            
            // 2. Create a bitboard containing ONLY this single knight
            uint64_t single_knight = (1ULL << src);
            
            // 3. Generate all 8 targets for THIS knight only
            uint64_t targets = 0;
            
            targets |= ((single_knight & NOT_A_FILE)  << 11);
            targets |= ((single_knight & NOT_F_FILE)  << 13);
            targets |= ((single_knight & NOT_AB_FILE) << 4);
            targets |= ((single_knight & NOT_EF_FILE) << 8);
            targets |= ((single_knight & NOT_F_FILE)  >> 11);
            targets |= ((single_knight & NOT_A_FILE)  >> 13);
            targets |= ((single_knight & NOT_EF_FILE) >> 4);
            targets |= ((single_knight & NOT_AB_FILE) >> 8);
            
            // 4. Mask the targets with valid_squares to filter out friendly fire and phantom bounds
            targets &= valid_squares;
            
            // 5. Extract the valid destinations for this specific knight
            while (targets) {
                int dst = __builtin_ctzll(targets);
                
                // Check if the destination contains a black piece to set the capture flag
                int flag = (state.b_occ & (1ULL << dst)) ? 1 : 0;
                
                moves.push_back(encode_move(src, dst, flag));
                
                targets &= (targets - 1); // Clear the processed target
            }
            
            // 6. Clear the processed knight and move to the next one
            knights_copy &= (knights_copy - 1); 
        }

        //BIshops moves: RAY CASTING METHODS:
        uint64_t bishops_copy = state.w_bishops;
        // Process each bishop one by one to preserve the exact 'src' square
        while (bishops_copy) {
            int src = __builtin_ctzll(bishops_copy);
            uint64_t single_bishop = (1ULL << src);
            uint64_t targets = 0;
            uint64_t ray;

            // 1. Up-Left Ray (Left Shift by 5)
            ray = (single_bishop & NOT_A_FILE) << 5; //single bishop so dont have to calculate ray by anding it to the boardmask, it can never be in both places at the same time
            while (ray & BOARD_MASK) {        // Enforce the 36-bit boundary!
                if (ray & state.w_occ) break; // Blocked by friendly piece -> stop ray
                
                targets |= ray;               // It's empty or an enemy -> valid square
                
                if (ray & state.b_occ) break; // Captured an enemy piece -> stop ray
                
                ray = (ray & NOT_A_FILE) << 5; // Step the ray one more square Up-Left
            }

            // 2. Up-Right Ray (Left Shift by 7)
            ray = (single_bishop & NOT_F_FILE) << 7;
            while (ray & BOARD_MASK) {
                if (ray & state.w_occ) break;
                targets |= ray;
                if (ray & state.b_occ) break;
                ray = (ray & NOT_F_FILE) << 7;
            }

            // 3. Down-Right Ray (Right Shift by 5)
            // Note: Right shifts drop bits into oblivion, so they don't need BOARD_MASK
            ray = (single_bishop & NOT_F_FILE) >> 5; 
            while (ray) { 
                if (ray & state.w_occ) break;
                targets |= ray;
                if (ray & state.b_occ) break;
                ray = (ray & NOT_F_FILE) >> 5;
            }

            // 4. Down-Left Ray (Right Shift by 7)
            ray = (single_bishop & NOT_A_FILE) >> 7;
            while (ray) {
                if (ray & state.w_occ) break;
                targets |= ray;
                if (ray & state.b_occ) break;
                ray = (ray & NOT_A_FILE) >> 7;
            }

            // Extract all valid destinations for THIS specific bishop
            while (targets) {
                int dst = __builtin_ctzll(targets);
                
                // Flag is 1 if destination contains a black piece, 0 otherwise
                int flag = (state.b_occ & (1ULL << dst)) ? 1 : 0; 
                
                moves.push_back(encode_move(src, dst, flag));
                
                targets &= (targets - 1); // Erase the processed target
            }

            bishops_copy &= (bishops_copy - 1); // Erase the processed bishop, move to next
        }
    
        if (state.w_queen){//Queen (no rooks) also ray casting, same as bishop + horizontal movements too, may have to isolate individual due to piece promotion: NOPE, PIECE PROMOTION CAN ONLY HAPPEN WHEN THAT PIECE ISNT THERE, THUS AT MAX 1 QUEEEN THROUGHOUT THE GAME
 
        int src = __builtin_ctzll(state.w_queen);
        uint64_t single_queen =  state.w_queen;
        uint64_t targets = 0;
        uint64_t ray;

        // 1. Up-Left Ray (Left Shift by 5)
        ray = (single_queen & NOT_A_FILE) << 5; //single queen so dont have to calculate ray by anding it to the boardmask, it can never be in both places at the same time
        while (ray & BOARD_MASK) {        // Enforce the 36-bit boundary!
            if (ray & state.w_occ) break; // Blocked by friendly piece -> stop ray
            
            targets |= ray;               // It's empty or an enemy -> valid square
            
            if (ray & state.b_occ) break; // Captured an enemy piece -> stop ray
            
            ray = (ray & NOT_A_FILE) << 5; // Step the ray one more square Up-Left
        }

        // 2. Up-Right Ray (Left Shift by 7)
        ray = (single_queen & NOT_F_FILE) << 7;
        while (ray & BOARD_MASK) {
            if (ray & state.w_occ) break;
            targets |= ray;
            if (ray & state.b_occ) break;
            ray = (ray & NOT_F_FILE) << 7;
        }

        // 3. Down-Right Ray (Right Shift by 5)
        // Note: Right shifts drop bits into oblivion, so they don't need BOARD_MASK
        ray = (single_queen & NOT_F_FILE) >> 5; 
        while (ray) { 
            if (ray & state.w_occ) break;
            targets |= ray;
            if (ray & state.b_occ) break;
            ray = (ray & NOT_F_FILE) >> 5;
        }

        // 4. Down-Left Ray (Right Shift by 7)
        ray = (single_queen & NOT_A_FILE) >> 7;
        while (ray) {
            if (ray & state.w_occ) break;
            targets |= ray;
            if (ray & state.b_occ) break;
            ray = (ray & NOT_A_FILE) >> 7;
        }

        // 5. Up Ray (Left Shift by 6)
        ray = (single_queen) << 6; //single queen so dont have to calculate ray by anding it to the boardmask, it can never be in both places at the same time
        while (ray & BOARD_MASK) {        // Enforce the 36-bit boundary!
            if (ray & state.w_occ) break; // Blocked by friendly piece -> stop ray
            
            targets |= ray;               // It's empty or an enemy -> valid square
            
            if (ray & state.b_occ) break; // Captured an enemy piece -> stop ray
            
            ray = (ray) << 6; // Step the ray one more square Up
        }

        //6. Down Ray (Right Shift by 6)
        ray = (single_queen) >> 6; //single queen so dont have to calculate ray by anding it to the boardmask, it can never be in both places at the same time
        while (ray & BOARD_MASK) {        // Enforce the 36-bit boundary!
            if (ray & state.w_occ) break; // Blocked by friendly piece -> stop ray
            
            targets |= ray;               // It's empty or an enemy -> valid square
            
            if (ray & state.b_occ) break; // Captured an enemy piece -> stop ray
            
            ray = (ray) >> 6; // Step the ray one more square Down
        }

        // 7. Left Ray (Right Shift by 1)
        ray = (single_queen & NOT_A_FILE) >> 1; //single queen so dont have to calculate ray by anding it to the boardmask, it can never be in both places at the same time
        while (ray & BOARD_MASK) {        // Enforce the 36-bit boundary!
            if (ray & state.w_occ) break; // Blocked by friendly piece -> stop ray
            
            targets |= ray;               // It's empty or an enemy -> valid square
            
            if (ray & state.b_occ) break; // Captured an enemy piece -> stop ray
            
            ray = (ray & NOT_A_FILE) >> 1; // Step the ray one more square Left
        }

        //8. Right Ray (Left Shift by 1)
        ray = (single_queen & NOT_F_FILE) << 1; //single queen so dont have to calculate ray by anding it to the boardmask, it can never be in both places at the same time
        while (ray) {        // Enforce the 36-bit boundary!
            if (ray & state.w_occ) break; // Blocked by friendly piece -> stop ray
            
            targets |= ray;               // It's empty or an enemy -> valid square
            
            if (ray & state.b_occ) break; // Captured an enemy piece -> stop ray
            
            ray = (ray & NOT_F_FILE) << 1; // Step the ray one more square Right
        }

        // Extract all valid destinations for THIS queen
        while (targets) {
            int dst = __builtin_ctzll(targets);
            
            // Flag is 1 if destination contains a black piece, 0 otherwise
            int flag = (state.b_occ & (1ULL << dst)) ? 1 : 0; 
            
            moves.push_back(encode_move(src, dst, flag));
            
            targets &= (targets - 1); // Erase the processed target
        }
        }
        
        if (state.w_king) {
        int src = __builtin_ctzll(state.w_king);
        uint64_t king = state.w_king;
        uint64_t targets = 0;
        
        // The master mask: anywhere on the board that isn't occupied by our own team
        uint64_t valid_squares = ~state.w_occ & BOARD_MASK;

        // 1. Up-Left (Shift Left by 5, protect A-File)
        targets |= ((king & NOT_A_FILE) << 5) & valid_squares;

        // 2. Up (Shift Left by 6, no file protection needed)
        targets |= (king << 6) & valid_squares;

        // 4. DOwn (Shift right by 6, no file protection needed)
        targets |= (king >> 6) & valid_squares;

        // 1. Down-Left (Shift Rt by 7, protect A-File)
        targets |= ((king & NOT_A_FILE) >> 7) & valid_squares;

        // 1. Up right (Shift left by 7, protect F-File)
        targets |= ((king & NOT_F_FILE) << 7) & valid_squares;

         // 1. Down right (Shift right by 5, protect F-File)
        targets |= ((king & NOT_F_FILE) >> 5) & valid_squares;
        
        // 1. Left (Shift Rt by 1, protect A-File)
        targets |= ((king & NOT_A_FILE) >> 1) & valid_squares;

        // 1. Up right (Shift left by 1, protect F-File)
        targets |= ((king & NOT_F_FILE) << 1) & valid_squares;
        // Finally, extract the targets
        while (targets) {
            int dst = __builtin_ctzll(targets);
            int flag = (state.b_occ & (1ULL << dst)) ? 1 : 0; 
            moves.push_back(encode_move(src, dst, flag));
            targets &= (targets - 1); 
        }
        }
    } else { // Black
        // --- PAWN PUSHES (excluding promotions) ---
        // Black pawns move DOWN (-6). Right shift by 6.
        uint64_t single_pushes = (state.b_pawns >> 6) & state.empty & ~RANK_1;
        
        uint64_t pushes_copy = single_pushes;
        while (pushes_copy) {
            int dst = __builtin_ctzll(pushes_copy);
            int src = dst + 6; // Reverse the downward shift to find origin
            moves.push_back(encode_move(src, dst));
            pushes_copy &= (pushes_copy - 1);
        }

        // --- PAWN CAPTURES (excluding promotions) ---
        // Down-Left (Right shift by 7). Protect the A-File.
        uint64_t down_left_captures = ((state.b_pawns & NOT_A_FILE) >> 7) & state.w_occ & ~RANK_1;
        uint64_t dl_captures_copy = down_left_captures;
        while (dl_captures_copy) {
            int dst = __builtin_ctzll(dl_captures_copy);
            int src = dst + 7; 
            moves.push_back(encode_move(src, dst, 1)); // 1 for capture flag
            dl_captures_copy &= (dl_captures_copy - 1); 
        }

        // Down-Right (Right shift by 5). Protect the F-File.
        uint64_t down_right_captures = ((state.b_pawns & NOT_F_FILE) >> 5) & state.w_occ & ~RANK_1;
        uint64_t dr_captures_copy = down_right_captures;
        while (dr_captures_copy) {
            int dst = __builtin_ctzll(dr_captures_copy);
            int src = dst + 5; 
            moves.push_back(encode_move(src, dst, 1));
            dr_captures_copy &= (dr_captures_copy - 1); 
        }

        // --- PAWN PROMOTIONS ---
        // A black pawn promotes when it reaches rank 1 (bits 0-5).
        // It can only promote to a piece type whose count is below the starting count.
        bool can_promo_knight = popcount(state.b_knights) < 2;
        bool can_promo_bishop = popcount(state.b_bishops) < 2;
        bool can_promo_queen  = state.b_queen == 0;

        if (can_promo_knight || can_promo_bishop || can_promo_queen) {
            // Promotion pushes
            uint64_t promo_pushes = (state.b_pawns >> 6) & state.empty & RANK_1;
            while (promo_pushes) {
                int dst = __builtin_ctzll(promo_pushes);
                int src = dst + 6;
                if (can_promo_knight) moves.push_back(encode_move(src, dst, 2));
                if (can_promo_bishop) moves.push_back(encode_move(src, dst, 3));
                if (can_promo_queen)  moves.push_back(encode_move(src, dst, 4));
                promo_pushes &= (promo_pushes - 1);
            }
            // Promotion captures (down-left)
            uint64_t promo_dl = ((state.b_pawns & NOT_A_FILE) >> 7) & state.w_occ & RANK_1;
            while (promo_dl) {
                int dst = __builtin_ctzll(promo_dl);
                int src = dst + 7;
                if (can_promo_knight) moves.push_back(encode_move(src, dst, 5));
                if (can_promo_bishop) moves.push_back(encode_move(src, dst, 6));
                if (can_promo_queen)  moves.push_back(encode_move(src, dst, 7));
                promo_dl &= (promo_dl - 1);
            }
            // Promotion captures (down-right)
            uint64_t promo_dr = ((state.b_pawns & NOT_F_FILE) >> 5) & state.w_occ & RANK_1;
            while (promo_dr) {
                int dst = __builtin_ctzll(promo_dr);
                int src = dst + 5;
                if (can_promo_knight) moves.push_back(encode_move(src, dst, 5));
                if (can_promo_bishop) moves.push_back(encode_move(src, dst, 6));
                if (can_promo_queen)  moves.push_back(encode_move(src, dst, 7));
                promo_dr &= (promo_dr - 1);
            }
        }

        // --- KNIGHT MOVES ---
        uint64_t knights_copy = state.b_knights;
        // Valid squares: anywhere on the board NOT occupied by our own Black pieces
        uint64_t valid_squares = ~state.b_occ & BOARD_MASK; 

        while (knights_copy) {
            int src = __builtin_ctzll(knights_copy);
            uint64_t single_knight = (1ULL << src);
            uint64_t targets = 0;
            
            // The 8 shift geometries are identical for White and Black
            targets |= ((single_knight & NOT_A_FILE)  << 11);
            targets |= ((single_knight & NOT_F_FILE)  << 13);
            targets |= ((single_knight & NOT_AB_FILE) << 4);
            targets |= ((single_knight & NOT_EF_FILE) << 8);
            targets |= ((single_knight & NOT_F_FILE)  >> 11);
            targets |= ((single_knight & NOT_A_FILE)  >> 13);
            targets |= ((single_knight & NOT_EF_FILE) >> 4);
            targets |= ((single_knight & NOT_AB_FILE) >> 8);
            
            targets &= valid_squares;
            
            while (targets) {
                int dst = __builtin_ctzll(targets);
                // Set flag to 1 if we are landing on a White piece
                int flag = (state.w_occ & (1ULL << dst)) ? 1 : 0;
                moves.push_back(encode_move(src, dst, flag));
                targets &= (targets - 1); 
            }
            knights_copy &= (knights_copy - 1); 
        }

        // --- BISHOP MOVES ---
        uint64_t bishops_copy = state.b_bishops;
        while (bishops_copy) {
            int src = __builtin_ctzll(bishops_copy);
            uint64_t single_bishop = (1ULL << src);
            uint64_t targets = 0;
            uint64_t ray;

            // 1. Up-Left (+5)
            ray = (single_bishop & NOT_A_FILE) << 5;
            while (ray & BOARD_MASK) {        
                if (ray & state.b_occ) break; // Blocked by BLACK -> stop
                targets |= ray;               
                if (ray & state.w_occ) break; // Captured WHITE -> stop
                ray = (ray & NOT_A_FILE) << 5; 
            }
            // 2. Up-Right (+7)
            ray = (single_bishop & NOT_F_FILE) << 7;
            while (ray & BOARD_MASK) {
                if (ray & state.b_occ) break;
                targets |= ray;
                if (ray & state.w_occ) break;
                ray = (ray & NOT_F_FILE) << 7;
            }
            // 3. Down-Right (-5)
            ray = (single_bishop & NOT_F_FILE) >> 5; 
            while (ray) { 
                if (ray & state.b_occ) break;
                targets |= ray;
                if (ray & state.w_occ) break;
                ray = (ray & NOT_F_FILE) >> 5;
            }
            // 4. Down-Left (-7)
            ray = (single_bishop & NOT_A_FILE) >> 7;
            while (ray) {
                if (ray & state.b_occ) break;
                targets |= ray;
                if (ray & state.w_occ) break;
                ray = (ray & NOT_A_FILE) >> 7;
            }

            while (targets) {
                int dst = __builtin_ctzll(targets);
                int flag = (state.w_occ & (1ULL << dst)) ? 1 : 0; 
                moves.push_back(encode_move(src, dst, flag));
                targets &= (targets - 1); 
            }
            bishops_copy &= (bishops_copy - 1); 
        }
        
        // --- QUEEN MOVES ---
        if (state.b_queen) {
            int src = __builtin_ctzll(state.b_queen);
            uint64_t single_queen = state.b_queen;
            uint64_t targets = 0;
            uint64_t ray;

            // Diagonals
            ray = (single_queen & NOT_A_FILE) << 5;
            while (ray & BOARD_MASK) {        
                if (ray & state.b_occ) break; 
                targets |= ray;               
                if (ray & state.w_occ) break; 
                ray = (ray & NOT_A_FILE) << 5; 
            }
            ray = (single_queen & NOT_F_FILE) << 7;
            while (ray & BOARD_MASK) {
                if (ray & state.b_occ) break;
                targets |= ray;
                if (ray & state.w_occ) break;
                ray = (ray & NOT_F_FILE) << 7;
            }
            ray = (single_queen & NOT_F_FILE) >> 5; 
            while (ray) { 
                if (ray & state.b_occ) break;
                targets |= ray;
                if (ray & state.w_occ) break;
                ray = (ray & NOT_F_FILE) >> 5;
            }
            ray = (single_queen & NOT_A_FILE) >> 7;
            while (ray) {
                if (ray & state.b_occ) break;
                targets |= ray;
                if (ray & state.w_occ) break;
                ray = (ray & NOT_A_FILE) >> 7;
            }

            // Straights
            ray = single_queen << 6; // Up
            while (ray & BOARD_MASK) {        
                if (ray & state.b_occ) break; 
                targets |= ray;               
                if (ray & state.w_occ) break; 
                ray <<= 6; 
            }
            ray = single_queen >> 6; // Down
            while (ray) {        
                if (ray & state.b_occ) break; 
                targets |= ray;               
                if (ray & state.w_occ) break; 
                ray >>= 6; 
            }
            ray = (single_queen & NOT_A_FILE) >> 1; // Left
            while (ray) {        
                if (ray & state.b_occ) break; 
                targets |= ray;               
                if (ray & state.w_occ) break; 
                ray = (ray & NOT_A_FILE) >> 1; 
            }
            ray = (single_queen & NOT_F_FILE) << 1; // Right
            while (ray & BOARD_MASK) {        
                if (ray & state.b_occ) break; 
                targets |= ray;               
                if (ray & state.w_occ) break; 
                ray = (ray & NOT_F_FILE) << 1; 
            }

            while (targets) {
                int dst = __builtin_ctzll(targets);
                int flag = (state.w_occ & (1ULL << dst)) ? 1 : 0; 
                moves.push_back(encode_move(src, dst, flag));
                targets &= (targets - 1); 
            }
        }
        
        // --- KING MOVES ---
        if (state.b_king) {
            int src = __builtin_ctzll(state.b_king);
            uint64_t king = state.b_king;
            uint64_t targets = 0;
            
            uint64_t valid_squares = ~state.b_occ & BOARD_MASK;

            targets |= ((king & NOT_A_FILE) << 5) & valid_squares; // Up-Left
            targets |= (king << 6) & valid_squares;                // Up
            targets |= ((king & NOT_F_FILE) << 7) & valid_squares; // Up-Right
            targets |= ((king & NOT_F_FILE) >> 5) & valid_squares; // Down-Right
            targets |= (king >> 6) & valid_squares;                // Down
            targets |= ((king & NOT_A_FILE) >> 7) & valid_squares; // Down-Left
            
            // NOTE: I added the horizontal shifts you missed for White here!
            // Make sure you add these two lines to your White King logic as well!
            targets |= ((king & NOT_A_FILE) >> 1) & valid_squares; // Left
            targets |= ((king & NOT_F_FILE) << 1) & valid_squares; // Right

            while (targets) {
                int dst = __builtin_ctzll(targets);
                int flag = (state.w_occ & (1ULL << dst)) ? 1 : 0; 
                moves.push_back(encode_move(src, dst, flag));
                targets &= (targets - 1); 
            }
        }
    }
    std::vector<uint16_t> legal_moves;
    for (uint16_t move : moves) {
        BitBoardState next_state = state;
        apply_move(next_state, move, side); // Simulate the move [cite: 8]
        
        // Discard the move if it results in our own King being in check 
        if (is_in_check(next_state, side) == false) {
            // Discard illegal moves (capturing the opponent's king is illegal)
            if (side == 1 && next_state.b_king == 0 && state.b_king != 0) continue;
            if (side == 0 && next_state.w_king == 0 && state.w_king != 0) continue;
            legal_moves.push_back(move);
        }
    }
    return legal_moves;
}