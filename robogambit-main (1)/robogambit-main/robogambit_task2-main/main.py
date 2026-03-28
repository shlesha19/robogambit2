"""
main.py — RoboGambit
Pipeline: perception (WiFi/TCP) → game engine → x,y,z → Serial JSON to arm ESP32
          solenoid engage/release via second serial port

Hardware:
  Arm ESP32      : /dev/tty.usbserial-110  (serial)
                   Sends: {"T":104,"x":...,"y":...,"z":...,"t":3.14,"spd":0.25}
  Solenoid ESP32 : /dev/tty.usbserial-120  (serial)
                   Sends: b'1' = engage (pick),  b'0' = release (place)
  Perception     : WiFi/TCP — handled entirely by perception.start_perception_loop()
"""

import json
import numpy as np
import serial
import time
import threading
import game
import perception
from queue import Queue

# ── Serial config ─────────────────────────────────────────────────────────────
ARM_PORT      = '/dev/tty.usbserial-110'
ARM_BAUD      = 115200
SOLENOID_PORT = '/dev/tty.usbserial-120'
SOLENOID_BAUD = 115200

# ── Arm command constants ─────────────────────────────────────────────────────
ARM_T   = 3.14    # wrist rotation (radians) — keeps gripper level
ARM_SPD = 0.25    # movement speed

# ── Board origin & square size (mm) ──────────────────────────────────────────
# Set ORIGIN_X/Y to the world coords of square A1 (col=0, row=0).
# Jog the arm manually to the centre of A1 and read off the x,y it reports.
ORIGIN_X       = 235.0
ORIGIN_Y       = 0.0
SQUARE_SIZE_MM = 50.0

# ── Z heights (mm) ────────────────────────────────────────────────────────────
Z_HOVER = 234.0   # travel height — arm moves between squares at this Z
Z_PICK  = 80.0    # descend to this Z to pick / place a piece
Z_HOME  = 234.0   # resting Z when idle

# ── Shared queue: perception pushes boards, main loop pulls them ──────────────
board_queue = Queue(maxsize=1)

# ── Column letter → index ─────────────────────────────────────────────────────
FILE_TO_COL = {'A': 0, 'B': 1, 'C': 2, 'D': 3, 'E': 4, 'F': 5}

PIECE_NAMES = {
    1: 'White Pawn',   2: 'White Knight', 3: 'White Bishop',
    4: 'White Queen',  5: 'White King',
    6: 'Black Pawn',   7: 'Black Knight', 8: 'Black Bishop',
    9: 'Black Queen', 10: 'Black King',
}

# ── Open serial ports ─────────────────────────────────────────────────────────
try:
    ser_arm = serial.Serial(ARM_PORT, baudrate=ARM_BAUD, timeout=2)
    #ser_arm.setRTS(False)
    #ser_arm.setDTR(False)
    print(f"[Serial] Arm connected on {ARM_PORT}")
except serial.SerialException as e:
    print(f"[Serial] WARNING — arm not available on {ARM_PORT}: {e}")
    ser_arm = None

try:
    ser_solenoid = serial.Serial(SOLENOID_PORT, baudrate=SOLENOID_BAUD, timeout=1)
    print(f"[Serial] Solenoid connected on {SOLENOID_PORT}")
except serial.SerialException as e:
    print(f"[Serial] WARNING — solenoid not available on {SOLENOID_PORT}: {e}")
    ser_solenoid = None


# ── Helpers ───────────────────────────────────────────────────────────────────

def grid_to_world(col: int, row: int) -> tuple:
    """Convert 0-indexed grid position to physical mm coordinates."""
    x = ORIGIN_X + col * SQUARE_SIZE_MM
    y = ORIGIN_Y + row * SQUARE_SIZE_MM
    return x, y


def send_xyz(x: float, y: float, z: float, retries: int = 3) -> None:
    """
    Send world-coordinate command to the arm ESP32 over serial.
    Format: {"T":104,"x":...,"y":...,"z":...,"t":3.14,"spd":0.25}
    """
    if ser_arm is None or not ser_arm.is_open:
        print(f"  [Arm] WARNING: serial unavailable — skipping ({x}, {y}, {z})")
        return

    cmd = json.dumps(
        {"T": 104, "x": round(x, 2), "y": round(y, 2),
         "z": round(z, 2), "t": ARM_T, "spd": ARM_SPD},
        separators=(',', ':')
    )
    print(f"  [Arm] -> {cmd}")

    for attempt in range(1, retries + 1):
        try:
            ser_arm.reset_input_buffer()
            ser_arm.write(cmd.encode() + b'\n')
            time.sleep(0.1)
            if ser_arm.in_waiting:
                ack = ser_arm.readline().decode(errors='replace').strip()
                if ack:
                    print(f"  [Arm] <- {ack}")
            return
        except serial.SerialException as e:
            print(f"  [Arm] Serial error (attempt {attempt}/{retries}): {e}")
            time.sleep(0.5)

    print("  [Arm] WARNING — all retries exhausted.")


def move_to(x: float, y: float, z: float, settle: float = 1.5) -> None:
    """Send coordinate command and wait for arm to physically reach position."""
    send_xyz(x, y, z)
    time.sleep(settle)


def home() -> None:
    print("  [Arm] Going home...")
    move_to(ORIGIN_X, 0.0, Z_HOME, settle=1.5)


def solenoid_engage() -> None:
    """Energise solenoid — picks up piece."""
    if ser_solenoid and ser_solenoid.is_open:
        ser_solenoid.write(b'1')
        print("  [Solenoid] ENGAGE")
    else:
        print("  [Solenoid] WARNING: unavailable")
    time.sleep(0.5)


def solenoid_release() -> None:
    """De-energise solenoid — releases piece."""
    if ser_solenoid and ser_solenoid.is_open:
        ser_solenoid.write(b'0')
        print("  [Solenoid] RELEASE")
    else:
        print("  [Solenoid] WARNING: unavailable")
    time.sleep(0.5)


def parse_move(move_str: str) -> dict:
    """
    '1:B2->B3'  →  { piece_id, piece_name,
                      src_cell, dst_cell,
                      src_coord:(col,row), dst_coord:(col,row) }
    """
    piece_id_str, rest = move_str.split(':')
    src_cell, dst_cell = rest.split('->')
    piece_id = int(piece_id_str)

    def cell_to_coord(cell):
        col = FILE_TO_COL[cell[0].upper()]
        row = int(cell[1]) - 1
        return (col, row)

    return {
        'piece_id':   piece_id,
        'piece_name': PIECE_NAMES.get(piece_id, f'Piece {piece_id}'),
        'src_cell':   src_cell,
        'dst_cell':   dst_cell,
        'src_coord':  cell_to_coord(src_cell),
        'dst_coord':  cell_to_coord(dst_cell),
    }


# ── Pick-and-place ────────────────────────────────────────────────────────────

def execute_move(board: np.ndarray) -> None:
    print("\n" + "=" * 60)

    if not np.any(board):
        print("Board is empty — waiting for pieces...")
        return

    # 1. Ask game engine for best move
    move_str = game.get_best_move(board, playing_white=True)
    if move_str is None:
        print("No valid move (game over or engine unavailable).")
        return

    # 2. Parse move → grid coords → world mm
    move_info        = parse_move(move_str)
    src_col, src_row = move_info['src_coord']
    dst_col, dst_row = move_info['dst_coord']
    src_x, src_y     = grid_to_world(src_col, src_row)
    dst_x, dst_y     = grid_to_world(dst_col, dst_row)

    print(f"  Move   : {move_str}  ({move_info['piece_name']})")
    print(f"  Source : {move_info['src_cell']}  →  x={src_x:.1f}  y={src_y:.1f}  mm")
    print(f"  Dest   : {move_info['dst_cell']}  →  x={dst_x:.1f}  y={dst_y:.1f}  mm")
    print()

    # 3. Pick-and-place sequence
    print("[1/7] Hover above source...")
    move_to(src_x, src_y, Z_HOVER, settle=1.5)

    print("[2/7] Descend to pick height...")
    move_to(src_x, src_y, Z_PICK, settle=1.0)

    print("[3/7] Engage solenoid...")
    solenoid_engage()

    print("[4/7] Lift piece...")
    move_to(src_x, src_y, Z_HOVER, settle=1.2)

    print("[5/7] Move to destination...")
    move_to(dst_x, dst_y, Z_HOVER, settle=1.5)

    print("[6/7] Descend to place height...")
    move_to(dst_x, dst_y, Z_PICK, settle=1.0)

    print("[7/7] Release piece...")
    solenoid_release()
    move_to(dst_x, dst_y, Z_HOVER, settle=0.8)
    home()

    print(f"Move '{move_str}' complete.\n")


# ── Main loop ─────────────────────────────────────────────────────────────────

if __name__ == "__main__":

    print("Starting perception thread (WiFi)...")
    t = threading.Thread(
        target=perception.start_perception_loop,
        args=(board_queue,),
        daemon=True
    )
    t.start()

    print("Waiting for first board detection...\n")

    try:
        while True:
            board = board_queue.get()
            print("\n[Main] Board received:")
            print(board)
            execute_move(board)
            time.sleep(1)

    except KeyboardInterrupt:
        print("\nStopping RoboGambit.")
        home()
        if ser_arm and ser_arm.is_open:
            ser_arm.close()
        if ser_solenoid and ser_solenoid.is_open:
            ser_solenoid.close()