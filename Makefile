# microWakeWord as a plugin of hassmic's: one static program for the Echos (32-bit ARM, musl) in a package (.hmp).
#
#   make                 build/microwakeword-<version>.hmp, signed with the author's key (KEY): what a release publishes,
#                        the asset the plugin index lists
#   make host            build/microwakeword-host: the same program for the PC, for the test
#   make test            the plugin in hassmic's PC build (HASSMIC: a hassmic tree with `make host` done and its .venv)
#   make release-test    the release's packing (tools/release.sh, as release.yml runs it) with a throwaway key, and the
#                        workflows' rules (tools/workflow_test.py)
#   make key             a new author's key pair: secrets/author.key (never in git), keys/author.pub
#   make sync | drift    take the SDK and the engine from HASSMIC | say where they differ (tools/sync.sh)
#
# A plugin is never built with the NDK: a static bionic program does not start in the sandbox.  PLUGIN_CC names a
# compiler that links musl statically; zig does when it is installed (one download, any OS), a musl cross gcc as well.
HASSMIC ?= ../echo-dot-assist
ID := microwakeword
VERSION := $(shell python3 -c 'import json; print(json.load(open("manifest.json"))["version"])')
PLUGIN_CC ?= $(if $(shell command -v zig 2>/dev/null),zig cc -target arm-linux-musleabihf -mcpu=cortex_a7,)
KEY ?= secrets/author.key

SRC := src/main.c src/mww_features.c src/mww_model.c src/json.c sdk/hmplugin.c
HDR := src/mww.h src/json.h sdk/hassmic_plugin.h sdk/plugmsg.h
# -z noexecstack: without PT_GNU_STACK the kernel makes every readable mapping executable, and hassmic refuses the file
CFLAGS := -O2 -Wall -Wextra -static -Wl,-z,noexecstack -Isdk -Isrc
PKG := build/$(ID)-$(VERSION).hmp

all: $(PKG)

build/$(ID): $(SRC) $(HDR)
	@mkdir -p build
	$(if $(PLUGIN_CC),,$(error no musl toolchain: install zig, or set PLUGIN_CC (for example a musl cross gcc)))
	$(PLUGIN_CC) $(CFLAGS) -s $(SRC) -lm -o $@
	python3 sdk/plugcheck.py elf $@

# The author's signature only: a package that installs as a sideload.  What makes it official is a listing signed
# by the plugin index's key, which the index adds to the package published here when it lists that version
# (README.md "Releases"; hassmic-plugins' tools/index.py)
$(PKG): build/$(ID) manifest.json LICENSE sdk/plugcheck.py
	$(if $(wildcard $(KEY)),,$(error $(KEY): no author's key (make key, or KEY=<file>)))
	python3 sdk/plugcheck.py pack manifest.json build/$(ID) $(KEY) $@ --license LICENSE

host: build/$(ID)-host
build/$(ID)-host: $(SRC) $(HDR)
	@mkdir -p build
	cc $(CFLAGS) $(SRC) -lm -o $@

test: build/$(ID)-host
	HASSMIC=$(abspath $(HASSMIC)) $(HASSMIC)/.venv/bin/python test.py

key:
	$(if $(wildcard secrets/author.key),$(error secrets/author.key is there: an Echo takes updates of an installed plugin from the key it was installed with only))
	@mkdir -p secrets keys
	python3 sdk/plugcheck.py keygen secrets/author.key keys/author.pub

# tools/release.sh as the release workflow runs it, with a throwaway key, and its refusals (tools/release_test.py);
# the release workflow and CI read as data (tools/workflow_test.py), and that test against broken copies of them,
# each of its checks (one rule each) switched off in turn (tools/workflow_breaks.py, some 20 to 40 s)
release-test:
	python3 tools/workflow_test.py
	python3 tools/workflow_breaks.py
	python3 tools/release_test.py

sync:
	tools/sync.sh $(HASSMIC)
drift:
	tools/sync.sh --check $(HASSMIC)

clean:
	rm -rf build

.PHONY: all host test release-test key sync drift clean
