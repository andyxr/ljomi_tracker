RACK_DIR ?= dep/Rack-SDK

FLAGS += -Isrc
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

# Copy the unpacked plugin straight into Rack's user plugin folder (Rack must be closed).
devinstall: all
	mkdir -p "$(PLUGINS_DIR)/$(SLUG)"
	cp -r $(TARGET) plugin.json res "$(PLUGINS_DIR)/$(SLUG)/"

test:
	sh tests/run.sh

.PHONY: devinstall test
