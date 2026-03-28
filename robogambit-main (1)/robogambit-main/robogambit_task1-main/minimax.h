#ifndef MINIMAX_H
#define MINIMAX_H

#include "bitboard_eval.h"
#include "move_application.h"
#include "move_generation.h"

int V(BitBoardState state, int depth, int alpha, int beta, int side);
uint16_t get_best_move(BitBoardState state, int depth, int side);

#endif