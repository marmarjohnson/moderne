PLATFORM ?= gabbro
TS_SOURCES := $(wildcard src/ts/*.ts) tsconfig.json
TS_OUTPUT := src/ts-build/index.js

.PHONY: help all build install ts clean git_unmanaged ctags etags settings gui

## Show this help (default)
help:
	@awk '/^## /{h=substr($$0,4); next} \
	      /^[a-zA-Z][a-zA-Z_-]*:/{if(h) printf "  %-10s %s\n", substr($$1,1,length($$1)-1), h; h=""; next} \
	      {h=""}' $(MAKEFILE_LIST)

## Build the watchapp and install it on the emulator
all: install

## Compile src/ts/*.ts -> src/ts-build/index.js (only if TS sources changed)
$(TS_OUTPUT): $(TS_SOURCES)
	./build_ts.sh

## Compile the TS/Clay config-page bundle
ts: $(TS_OUTPUT)

## Build the watchapp for all targetPlatforms
build: $(TS_OUTPUT)
	pebble build

## Install the watchapp on the emulator (builds first; PLATFORM=gabbro only)
install: build
	pebble install --emulator $(PLATFORM)

## Remove derived build objects (pebble's build/, .lock-waf_linux_build, and src/ts-build/) --
## pebble clean does NOT remove src/ts-build/index.js, so that's done separately here
clean:
	pebble clean
	rm -f $(TS_OUTPUT)

## List files not tracked by git
git_unmanaged:
	git ls-files --others --exclude-standard

## Generate a ctags index for the whole project (excludes build/ and node_modules/)
ctags:
	ctags -R --exclude=build --exclude=node_modules --exclude=.git .

## Generate an etags (Emacs) index for the whole project (excludes build/ and node_modules/)
etags:
	find . \( -path ./build -o -path ./node_modules -o -path ./.git \) -prune -o -type f -print | etags -

## Start (or restart) the emulator settings GUI (emu_settings_gui.py)
## Uses pebble-tool's own python (it imports pebble_tool/libpebble2), found
## via the `pebble` command's shebang.
settings:
	-pkill -f 'python.*emu_settings_gui\.py' 2>/dev/null
	sleep 1
	nohup $$(head -1 "$$(which pebble)" | cut -c3-) emu_settings_gui.py > /tmp/emu_settings_gui.log 2>&1 &
	sleep 1
	@echo "settings GUI (re)started -- see http://localhost:8765/ (log: /tmp/emu_settings_gui.log)"

## Alias for 'settings'
gui: settings
