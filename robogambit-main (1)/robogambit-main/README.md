# RoboGambit Chess Engine

## Overview

RoboGambit is a chess engine implemented using **C++ for the core engine logic** and **Python for the graphical interface**.
The engine uses a **bitboard-based board representation** and a **minimax search with alpha-beta pruning** to evaluate positions and select moves.

The graphical interface is implemented using Python and allows a user to play against the engine.

---

## Requirements

Before running the project, make sure the following are installed:

* Python 3.x
* C++ compiler (clang / g++)
* CMake
* pybind11
* pip (Python package manager)

---

## Installation

### 1. Install CMake

CMake is required to build the C++ chess engine.

```id="mre2f4"
pip install cmake
```

Alternatively, you can install it through your system package manager.

---

### 2. Install pybind11

pybind11 is used to connect the C++ chess engine with Python.

```id="3f9u2j"
python3 -m pip install pybind11
```

---

### 3. Build the C++ Engine

Navigate to the project directory and compile the engine.

```id="7ip9r5"
make
```

This step compiles the C++ code and creates a Python module that the GUI can use.

---

## Running the Program

### Running the Game Logic

To start the chess engine and game logic, run:

```id="3u7s9c"
python3 game.py
```

This script runs the main game loop and manages the interaction between the user and the engine.

---

### Running the Visualiser

To open the graphical chess board interface, run:

```id="2o9n8g"
python3 visualiser.py
```

This script launches the GUI that displays the chess board and allows the user to interact with the game.

---

## How to Play / Testing the Engine

1. The **user plays one side of the board**.

2. To make a move:

   * Click on the piece you want to move.
   * Click on the destination square.

3. After the user move is completed:

   * Press the **Spacebar**.
   * The engine will calculate and make its move.

4. Moves occur **alternately** between the user and the engine.

---

## Notes

* The engine move is triggered **manually by pressing the Spacebar**.
* If the engine module is not compiled properly, the GUI may open but the engine will not respond.
* Ensure that the build step (`make`) is completed successfully before running the visualiser.