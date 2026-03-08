PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
MANDIR ?= $(DATADIR)/man
SESSION_DIR ?= $(DATADIR)/wayland-sessions
XSESSION_DIR ?= $(DATADIR)/xsessions
BUILD_TYPE ?= Release
BUILD_DIR ?= build
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
INSTALLED_BINARIES := \
	$(DESTDIR)$(BINDIR)/eternal \
	$(DESTDIR)$(BINDIR)/eternalctl \
	$(DESTDIR)$(BINDIR)/eternal-session
INSTALLED_DATA := \
	$(DESTDIR)$(DATADIR)/eternal/eternal.kdl \
	$(DESTDIR)$(SESSION_DIR)/eternal.desktop \
	$(DESTDIR)$(MANDIR)/man1/eternal.1
LEGACY_SESSION_FILES := \
	$(DESTDIR)$(SESSION_DIR)/wm.desktop \
	$(DESTDIR)$(SESSION_DIR)/eternal.desktop \
	$(DESTDIR)$(XSESSION_DIR)/wm.desktop \
	$(DESTDIR)$(XSESSION_DIR)/eternal.desktop

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

install:
	rm -rf $(BUILD_DIR)
	rm -f $(INSTALLED_BINARIES) $(INSTALLED_DATA) $(LEGACY_SESSION_FILES)
	cmake -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		-DETERNAL_BUILD_TESTS=OFF
	cmake --build $(BUILD_DIR) -j$(JOBS)
	install -Dm755 $(BUILD_DIR)/eternal $(DESTDIR)$(BINDIR)/eternal
	install -Dm755 $(BUILD_DIR)/eternalctl $(DESTDIR)$(BINDIR)/eternalctl
	install -Dm755 packaging/eternal-session $(DESTDIR)$(BINDIR)/eternal-session
	install -Dm644 config/eternal.kdl $(DESTDIR)$(DATADIR)/eternal/eternal.kdl
	install -Dm644 packaging/eternal.desktop $(DESTDIR)$(SESSION_DIR)/eternal.desktop
	install -Dm644 docs/eternal.1 $(DESTDIR)$(MANDIR)/man1/eternal.1

uninstall:
	rm -f $(INSTALLED_BINARIES) $(INSTALLED_DATA) $(LEGACY_SESSION_FILES)

clean:
	rm -rf $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/eternal

run-headless: build
	WLR_BACKENDS=headless WLR_RENDERER=pixman ./$(BUILD_DIR)/eternal

.PHONY: all configure build debug test install uninstall clean run run-headless
