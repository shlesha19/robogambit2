PYTHON ?= python
CXX ?= g++

MODULE := robogambit_cpp
SOURCES := bindings.cpp ../robogambit_task1-main/bitboard_eval.cpp ../robogambit_task1-main/move_application.cpp ../robogambit_task1-main/move_generation.cpp ../robogambit_task1-main/minimax.cpp
HEADERS := ../robogambit_task1-main/bitboard_eval.h ../robogambit_task1-main/minimax.h ../robogambit_task1-main/move_application.h ../robogambit_task1-main/move_generation.h 
LOCAL_INCLUDES := -I../robogambit_task1-main

PY_INCLUDES := $(shell $(PYTHON) -m pybind11 --includes)
EXT_SUFFIX := $(shell $(PYTHON) -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or '.pyd')")
TARGET := $(MODULE)$(EXT_SUFFIX)

CXXFLAGS ?= -O3 -Wall -shared -std=c++17

# Platform-specific flags
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -DMS_WIN64
    PY_LDFLAGS := $(shell $(PYTHON) -c "import sysconfig, os; prefix=sysconfig.get_config_var('prefix'); print('-L' + os.path.join(prefix, 'libs') + ' -lpython' + sysconfig.get_config_var('py_version_nodot'))")
else
    CXXFLAGS += -fPIC
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        CXXFLAGS += -undefined dynamic_lookup
    endif
    PY_LDFLAGS :=
endif

.PHONY: all clean run help

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(PY_INCLUDES) $(LOCAL_INCLUDES) $(SOURCES) -o $@ $(PY_LDFLAGS)

clean:
ifeq ($(OS),Windows_NT)
	-del /Q $(TARGET) 2>nul
	-del /Q *.o 2>nul
	-del /Q *.obj 2>nul
else
	-$(RM) $(TARGET)
	-$(RM) *.o
	-$(RM) *.obj
endif

run:
	$(PYTHON) visualiser.py

help:
	@echo "Targets:"
	@echo "  make all   - Build the robogambit_cpp Python extension"
	@echo "  make run   - Run the Python visualiser"
	@echo "  make clean - Remove build artifacts"
