PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
MANDIR ?= $(DATADIR)/man
BUILD_TYPE ?= Release
BUILD_DIR ?= build
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

all: build

configure:
	@cmake -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		-DETERNAL_BUILD_TESTS=OFF

build: configure
	@cmake --build $(BUILD_DIR) -j$(JOBS)

debug:
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DETERNAL_BUILD_TESTS=OFF
	@cmake --build $(BUILD_DIR) -j$(JOBS)

test:
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DETERNAL_BUILD_TESTS=ON
	@cmake --build $(BUILD_DIR) -j$(JOBS)
	@cd $(BUILD_DIR) && ctest --output-on-failure

install: build
	install -Dm755 $(BUILD_DIR)/eternal $(DESTDIR)$(BINDIR)/eternal
	install -Dm755 $(BUILD_DIR)/eternalctl $(DESTDIR)$(BINDIR)/eternalctl
	install -Dm644 config/eternal.kdl $(DESTDIR)$(DATADIR)/eternal/eternal.kdl
	install -Dm644 packaging/eternal.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/eternal.desktop
	install -Dm644 docs/eternal.1 $(DESTDIR)$(MANDIR)/man1/eternal.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/eternal
	rm -f $(DESTDIR)$(BINDIR)/eternalctl
	rm -f $(DESTDIR)$(DATADIR)/eternal/eternal.kdl
	rm -f $(DESTDIR)$(DATADIR)/wayland-sessions/eternal.desktop
	rm -f $(DESTDIR)$(MANDIR)/man1/eternal.1

clean:
	rm -rf $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/eternal

run-headless: build
	WLR_BACKENDS=headless WLR_RENDERER=pixman ./$(BUILD_DIR)/eternal

.PHONY: all configure build debug test install uninstall clean run run-headless
