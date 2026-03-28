import pygame
import sys
import os
import numpy as np

# Import your PyBind11 C++ module (Ensure the .pyd/.so is in the same folder)
try:
    # On Windows, Python 3.8+ needs explicit DLL search paths for MinGW runtime
    if sys.platform == "win32":
        mingw_bin = os.path.join(os.environ.get("MSYS2_ROOT", r"C:\msys64"), "mingw64", "bin")
        if os.path.isdir(mingw_bin):
            os.add_dll_directory(mingw_bin)
    import robogambit_cpp
    ENGINE_AVAILABLE = True
except ImportError:
    print("WARNING: robogambit_cpp module not found. Engine moves disabled.")
    ENGINE_AVAILABLE = False

# ---------------------------------------------------------------------------
# Constants & Configuration
# ---------------------------------------------------------------------------
BOARD_SIZE = 6
SQUARE_SIZE = 100  # Adjust this to make the window larger/smaller
WIDTH = BOARD_SIZE * SQUARE_SIZE
HEIGHT = BOARD_SIZE * SQUARE_SIZE

# Colors
COLOR_LIGHT = (240, 217, 181)
COLOR_DARK = (181, 136, 99)
COLOR_HIGHLIGHT = (186, 202, 68)

# # Map NumPy integer IDs to string names for the image loader
# PIECE_MAP = {
#     1: "wP", 2: "wN", 3: "wB", 4: "wQ", 5: "wK",
#     6: "bP", 7: "bN", 8: "bB", 9: "bQ", 10: "bK"
# }

# Column index → letter (for string generation)
COL_TO_FILE = {0: 'A', 1: 'B', 2: 'C', 3: 'D', 4: 'E', 5: 'F'}

# Map NumPy integer IDs directly to Unicode characters
# 1-5: White (Outlined), 6-10: Black (Filled)
UNICODE_PIECES = {
    1: '♙', 2: '♘', 3: '♗', 4: '♕', 5: '♔', 
    6: '♟', 7: '♞', 8: '♝', 9: '♛', 10: '♚'
}

# ---------------------------------------------------------------------------
# Core Functions
# ---------------------------------------------------------------------------


def draw_board(screen):
    """Draws the 6x6 checkered background."""
    colors = [COLOR_LIGHT, COLOR_DARK]
    for row in range(BOARD_SIZE):
        for col in range(BOARD_SIZE):
            color = colors[((row + col) % 2)]
            pygame.draw.rect(screen, color, pygame.Rect(col * SQUARE_SIZE, row * SQUARE_SIZE, SQUARE_SIZE, SQUARE_SIZE))

def highlight_squares(screen, selected_sq, valid_moves=None):
    """Highlights the currently selected square."""
    if selected_sq != ():
        np_row, np_col = selected_sq
        # Invert NumPy row back to Pygame row for drawing
        py_row = (BOARD_SIZE - 1) - np_row 
        
        # Create a semi-transparent highlight surface
        s = pygame.Surface((SQUARE_SIZE, SQUARE_SIZE))
        s.set_alpha(150) 
        s.fill(COLOR_HIGHLIGHT)
        screen.blit(s, (np_col * SQUARE_SIZE, py_row * SQUARE_SIZE))
def draw_pieces(screen, board):
    """
    Renders Unicode chess characters based on the NumPy array IDs.
    """
    # Initialize a font. We pass a list of fallback fonts known to support chess Unicode.
    # The size is scaled slightly smaller than the square to fit perfectly.
    font_size = int(SQUARE_SIZE * 0.85)
    font = pygame.font.Font("DejaVuSans.ttf", font_size)

    for np_row in range(BOARD_SIZE):
        for np_col in range(BOARD_SIZE):
            piece_id = board[np_row][np_col]
            if piece_id != 0:
                # Coordinate inversion: Pygame Top is NumPy Bottom
                py_row = (BOARD_SIZE - 1) - np_row 
                
                # Render the Unicode character (Black text for both, since the Unicode 
                # characters themselves are inherently hollow for White and filled for Black)
                text_surf = font.render(UNICODE_PIECES[piece_id], True, (0, 0, 0))
                
                # Center the text surface inside the square
                square_center_x = (np_col * SQUARE_SIZE) + (SQUARE_SIZE // 2)
                square_center_y = (py_row * SQUARE_SIZE) + (SQUARE_SIZE // 2)
                text_rect = text_surf.get_rect(center=(square_center_x, square_center_y))
                
                screen.blit(text_surf, text_rect)
            
def draw_game_state(screen, board, selected_sq):
    """Master rendering pipeline."""
    draw_board(screen)
    highlight_squares(screen, selected_sq)
    draw_pieces(screen, board)

# ---------------------------------------------------------------------------
# Interaction Helpers
# ---------------------------------------------------------------------------

def count_pieces(board, piece_id):
    """Count occurrences of a piece_id on the board."""
    return int(np.count_nonzero(board == piece_id))

def get_promotion_choices(board, is_white):
    """Return list of (piece_id, label) the pawn can promote to (only captured pieces)."""
    choices = []
    if is_white:
        if count_pieces(board, 2) < 2: choices.append((2, '♘ Knight'))
        if count_pieces(board, 3) < 2: choices.append((3, '♗ Bishop'))
        if count_pieces(board, 4) < 1: choices.append((4, '♕ Queen'))
    else:
        if count_pieces(board, 7) < 2: choices.append((7, '♞ Knight'))
        if count_pieces(board, 8) < 2: choices.append((8, '♝ Bishop'))
        if count_pieces(board, 9) < 1: choices.append((9, '♛ Queen'))
    return choices

def draw_promotion_popup(screen, choices):
    """Draw a simple promotion selection popup and return the chosen piece_id."""
    font = pygame.font.SysFont(None, 36)
    popup_w, popup_h = 200, 50 * len(choices)
    popup_x = (WIDTH - popup_w) // 2
    popup_y = (HEIGHT - popup_h) // 2

    selecting = True
    while selecting:
        # Draw popup background
        pygame.draw.rect(screen, (50, 50, 50), (popup_x, popup_y, popup_w, popup_h))
        pygame.draw.rect(screen, (255, 255, 255), (popup_x, popup_y, popup_w, popup_h), 2)

        rects = []
        for i, (pid, label) in enumerate(choices):
            r = pygame.Rect(popup_x, popup_y + i * 50, popup_w, 50)
            rects.append((r, pid))
            text = font.render(label, True, (255, 255, 255))
            screen.blit(text, text.get_rect(center=r.center))

        pygame.display.flip()

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                sys.exit()
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for r, pid in rects:
                    if r.collidepoint(pos):
                        return pid
    return choices[0][0]

def apply_move_locally(board, src_tuple, dst_tuple, screen=None):
    """Applies a human click-move to the numpy array, with promotion handling."""
    src_row, src_col = src_tuple
    dst_row, dst_col = dst_tuple
    
    piece = board[src_row][src_col]
    is_white_piece = (piece == 1)  # white pawn
    is_black_piece = (piece == 6)  # black pawn
    
    # Check if this is a pawn reaching the promotion rank
    if (is_white_piece and dst_row == 5) or (is_black_piece and dst_row == 0):
        choices = get_promotion_choices(board, is_white_piece)
        if choices:
            if screen is not None:
                promo_id = draw_promotion_popup(screen, choices)
            else:
                promo_id = choices[0][0]  # fallback: pick first available
            board[src_row][src_col] = 0
            board[dst_row][dst_col] = promo_id
            return board
        else:
            # No captured pieces to promote to — move is illegal, do nothing
            return board
    
    board[src_row][src_col] = 0
    board[dst_row][dst_col] = piece
    return board

def parse_and_apply_engine_string(board, move_str):
    """Applies the C++ engine's '1:A2->A3' or '1:A5->A6=4' string to the numpy array."""
    if move_str == "None" or move_str is None:
        return board
        
    parts = move_str.split(':')
    move_part = parts[1]  # e.g. "A5->A6=4" or "A2->A3"
    
    # Check for promotion suffix
    promo_id = None
    if '=' in move_part:
        move_part, promo_str = move_part.split('=')
        promo_id = int(promo_str)
    
    cells = move_part.split('->')
    
    # "A2" -> Col 0, Row 1
    src_col = ord(cells[0][0]) - ord('A')
    src_row = int(cells[0][1]) - 1
    dst_col = ord(cells[1][0]) - ord('A')
    dst_row = int(cells[1][1]) - 1
    
    board[src_row][src_col] = 0
    if promo_id is not None:
        board[dst_row][dst_col] = promo_id  # Place the promoted piece
    else:
        piece = int(parts[0])  # piece_id from the string prefix
        board[dst_row][dst_col] = piece
    return board

# ---------------------------------------------------------------------------
# Main Application Loop
# ---------------------------------------------------------------------------

def main():
    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("RoboGambit 6x6 C++ Engine Interface")
    clock = pygame.time.Clock()

    # The official tournament starting position
    board = np.array([
        [ 2,  3,  4,  5,  3,  2],  # Row 1 (A1–F1) — White back rank
        [ 1,  1,  1,  1,  1,  1],  # Row 2         — White pawns
        [ 0,  0,  0,  0,  0,  0],  # Row 3
        [ 0,  0,  0,  0,  0,  0],  # Row 4
        [ 6,  6,  6,  6,  6,  6],  # Row 5         — Black pawns
        [ 7,  8,  9, 10,  8,  7],  # Row 6 (A6–F6) — Black back rank
    ], dtype=int)

    sq_selected = () # Tuple: (np_row, np_col)
    player_clicks = [] # Array of tuples: [(src_row, src_col), (dst_row, dst_col)]
    
    is_white_turn = True
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                
            # 1. HUMAN INTERACTION (Mouse Clicks)
            elif event.type == pygame.MOUSEBUTTONDOWN:
                location = pygame.mouse.get_pos() # (x, y)
                py_col = location[0] // SQUARE_SIZE
                py_row = location[1] // SQUARE_SIZE
                
                # Invert Pygame click back to NumPy coordinates
                np_col = py_col
                np_row = (BOARD_SIZE - 1) - py_row 
                
                if sq_selected == (np_row, np_col): # User clicked same square twice (deselect)
                    sq_selected = ()
                    player_clicks = []
                else:
                    sq_selected = (np_row, np_col)
                    player_clicks.append(sq_selected)
                
                if len(player_clicks) == 2: # We have a Source and a Destination
                    board = apply_move_locally(board, player_clicks[0], player_clicks[1], screen)
                    
                    # Reset clicks and pass turn
                    sq_selected = ()
                    player_clicks = []
                    is_white_turn = not is_white_turn

            # 2. ENGINE INTERACTION (Press Spacebar)
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_SPACE:
                    if ENGINE_AVAILABLE:
                        # Debug: count legal moves before searching
                        legal_count = robogambit_cpp.count_legal_moves(board, is_white_turn)
                        print(f"{'White' if is_white_turn else 'Black'} has {legal_count} legal moves")
                        
                        print("C++ Engine is calculating...")
                        # Pass the NumPy array directly to the PyBind module
                        move_str = robogambit_cpp.get_best_move(board, is_white_turn)
                        print(f"Engine played: {move_str}")
                        
                        if move_str != "None":
                            board = parse_and_apply_engine_string(board, move_str)
                            is_white_turn = not is_white_turn
                    else:
                        print("Cannot calculate move: C++ module not linked.")

        # Render the frame
        draw_game_state(screen, board, sq_selected)
        pygame.display.flip()
        
        # Cap at 30 FPS to prevent burning CPU cycles unnecessarily
        clock.tick(30)

    pygame.quit()
    sys.exit()

if __name__ == "__main__":
    main()