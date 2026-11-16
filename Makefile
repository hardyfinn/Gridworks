# Path to the Rack SDK (override on the command line: make RACK_DIR=...)
RACK_DIR ?= ../Rack-SDK

FLAGS +=
SOURCES += $(wildcard src/*.cpp) $(wildcard src/grid/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/arch.mk
ifdef ARCH_WIN
	LDFLAGS += -lws2_32
endif

include $(RACK_DIR)/plugin.mk
