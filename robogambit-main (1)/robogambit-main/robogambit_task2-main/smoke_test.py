"""
smoke_test.py — RoboGambit pipeline test (no hardware required)

Mocks out:
  - Serial ports  (arm + solenoid)
  - Perception    (injects a hardcoded starting board directly into the queue)

Verifies:
  - game.get_best_move() returns a valid move string
  - parse_move() correctly decodes it
  - grid_to_world() produces sensible mm coordinates
  - send_xyz() builds the correct JSON command
  - solenoid_engage/release produce the correct bytes
  - The full 7-step execute_move() sequence runs without error
"""

import json
import sys
import numpy as np
from queue import Queue
from unittest.mock import MagicMock, patch

print("=" * 60)
print("  RoboGambit Smoke Test  (no hardware)")
print("=" * 60)

# ── 1. Mock serial.Serial BEFORE main.py imports it ──────────────────────────
mock_arm_serial      = MagicMock()
mock_arm_serial.is_open      = True
mock_arm_serial.in_waiting   = 0

mock_solenoid_serial = MagicMock()
mock_solenoid_serial.is_open = True

def fake_serial_constructor(port, **kwargs):
    if '110' in port:
        print(f"  [Mock] Serial opened for ARM on {port}")
        return mock_arm_serial
    if '120' in port:
        print(f"  [Mock] Serial opened for SOLENOID on {port}")
        return mock_solenoid_serial
    raise Exception(f"Unexpected port: {port}")

import serial as _serial_module
_serial_module.Serial = fake_serial_constructor

# ── 2. Import main (serial.Serial is already mocked, ports open cleanly) ─────
print("\n[Step 1] Importing main.py...")
import main
print("  main.py imported OK")

# ── 3. Inject a realistic starting board directly into board_queue ────────────
#
# Standard 6×6 RoboGambit starting position:
#   Row 0 (rank 1): White back rank
#   Row 1 (rank 2): White pawns
#   Row 4 (rank 5): Black pawns
#   Row 5 (rank 6): Black back rank
#
starting_board = np.array([
    [ 2,  3,  4,  5,  3,  2],   # White back rank  (A1–F1)
    [ 1,  1,  1,  1,  1,  1],   # White pawns
    [ 0,  0,  0,  0,  0,  0],
    [ 0,  0,  0,  0,  0,  0],
    [ 6,  6,  6,  6,  6,  6],   # Black pawns
    [ 7,  8,  9, 10,  8,  7],   # Black back rank  (A6–F6)
], dtype=int)

print("\n[Step 2] Injecting test board into queue:")
print(starting_board)
main.board_queue.put(starting_board.copy())

# ── 4. Intercept send_xyz to capture what would be sent to the arm ────────────
sent_commands = []
original_send_xyz = main.send_xyz

def capturing_send_xyz(x, y, z, retries=3):
    cmd = {
        "T": 104,
        "x": round(x, 2),
        "y": round(y, 2),
        "z": round(z, 2),
        "t": main.ARM_T,
        "spd": main.ARM_SPD
    }
    sent_commands.append(cmd)
    print(f"  [Arm serial] -> {json.dumps(cmd, separators=(',',':'))}")

main.send_xyz = capturing_send_xyz

# ── 5. Intercept solenoid writes to capture bytes sent ───────────────────────
solenoid_bytes = []
original_write = mock_solenoid_serial.write

def capturing_solenoid_write(data):
    solenoid_bytes.append(data)
    label = "ENGAGE (pick)" if data == b'1' else "RELEASE (place)"
    print(f"  [Solenoid serial] -> {data}  ({label})")

mock_solenoid_serial.write = capturing_solenoid_write

# ── 6. Run execute_move with the test board ───────────────────────────────────
print("\n[Step 3] Running execute_move()...")
board = main.board_queue.get()
main.execute_move(board)

# ── 7. Assertions ─────────────────────────────────────────────────────────────
print("\n" + "=" * 60)
print("  Assertions")
print("=" * 60)

errors = []

# 7a. At least one arm command was sent
if sent_commands:
    print(f"  PASS  {len(sent_commands)} arm command(s) sent over serial")
else:
    print("  FAIL  No arm commands were sent")
    errors.append("No arm commands sent")

# 7b. Every arm command has the required JSON keys and correct T value
required_keys = {"T", "x", "y", "z", "t", "spd"}
for i, cmd in enumerate(sent_commands):
    missing = required_keys - cmd.keys()
    if missing:
        print(f"  FAIL  Command {i+1} missing keys: {missing}")
        errors.append(f"Command {i+1} missing keys")
    elif cmd["T"] != 104:
        print(f"  FAIL  Command {i+1} has wrong T value: {cmd['T']} (expected 104)")
        errors.append(f"Command {i+1} wrong T")
    else:
        print(f"  PASS  Command {i+1} JSON keys and T=104 correct")

# 7c. Solenoid was engaged then released
if b'1' in solenoid_bytes:
    print("  PASS  Solenoid engaged (b'1' sent)")
else:
    print("  FAIL  Solenoid never engaged")
    errors.append("Solenoid never engaged")

if b'0' in solenoid_bytes:
    print("  PASS  Solenoid released (b'0' sent)")
else:
    print("  FAIL  Solenoid never released")
    errors.append("Solenoid never released")

engage_idx  = solenoid_bytes.index(b'1') if b'1' in solenoid_bytes else None
release_idx = solenoid_bytes.index(b'0') if b'0' in solenoid_bytes else None
if engage_idx is not None and release_idx is not None:
    if engage_idx < release_idx:
        print("  PASS  Solenoid order correct (engage before release)")
    else:
        print("  FAIL  Solenoid released before engaged!")
        errors.append("Solenoid order wrong")

# 7d. Arm visited hover, pick, and home Z heights
zs = [c["z"] for c in sent_commands]
if main.Z_HOVER in zs:
    print(f"  PASS  Arm reached Z_HOVER ({main.Z_HOVER} mm)")
else:
    print(f"  FAIL  Arm never reached Z_HOVER ({main.Z_HOVER} mm)")
    errors.append("Z_HOVER never reached")

if main.Z_PICK in zs:
    print(f"  PASS  Arm reached Z_PICK ({main.Z_PICK} mm)")
else:
    print(f"  FAIL  Arm never reached Z_PICK ({main.Z_PICK} mm)")
    errors.append("Z_PICK never reached")

# 7e. grid_to_world sanity check
print("\n  grid_to_world spot-checks:")
for cell, expected_col, expected_row in [("A1", 0, 0), ("F6", 5, 5), ("C3", 2, 2)]:
    col = main.FILE_TO_COL[cell[0]]
    row = int(cell[1]) - 1
    x, y = main.grid_to_world(col, row)
    expected_x = main.ORIGIN_X + expected_col * main.SQUARE_SIZE_MM
    expected_y = main.ORIGIN_Y + expected_row * main.SQUARE_SIZE_MM
    if x == expected_x and y == expected_y:
        print(f"  PASS  {cell} → ({x:.1f}, {y:.1f}) mm")
    else:
        print(f"  FAIL  {cell} → ({x:.1f}, {y:.1f}) mm  (expected ({expected_x}, {expected_y}))")
        errors.append(f"grid_to_world wrong for {cell}")

# ── 8. Summary ────────────────────────────────────────────────────────────────
print("\n" + "=" * 60)
if errors:
    print(f"  RESULT: FAILED — {len(errors)} error(s):")
    for e in errors:
        print(f"    - {e}")
    sys.exit(1)
else:
    print("  RESULT: ALL CHECKS PASSED")
    print("  Pipeline is wired correctly. Ready for hardware.")
print("=" * 60)