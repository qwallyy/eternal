#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
}

struct wlr_data_device_manager;
struct wlr_primary_selection_v1_device_manager;
struct wlr_seat;

namespace eternal {

class Server;

// ---------------------------------------------------------------------------
// Clipboard entry for history
// ---------------------------------------------------------------------------

struct ClipboardEntry {
    std::vector<std::string> mimeTypes;
    std::vector<uint8_t> data;
    uint64_t timestamp = 0;
};

// ---------------------------------------------------------------------------
// Clipboard - wl_data_device + primary selection management
// ---------------------------------------------------------------------------

class Clipboard {
public:
    explicit Clipboard(Server& server);
    ~Clipboard();

    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    /// Initialize clipboard protocols.
    bool init();

    /// Shutdown clipboard management.
    void shutdown();

    // ── wl_data_device (Task 94) ─────────────────────────────────────────

    /// Get the data device manager.
    [[nodiscard]] wlr_data_device_manager* getDataDeviceManager() const {
        return m_dataDeviceManager;
    }

    /// Get the current clipboard MIME types.
    [[nodiscard]] std::vector<std::string> getClipboardMimeTypes() const;

    /// Read clipboard data for a specific MIME type.
    /// Returns the data, or empty vector if unavailable.
    [[nodiscard]] std::vector<uint8_t> readClipboard(
        const std::string& mimeType) const;

    /// Set clipboard data with the given MIME types.
    bool setClipboard(const std::vector<std::string>& mimeTypes,
                      const std::vector<uint8_t>& data);

    /// Clear the clipboard selection.
    void clearClipboard();

    /// Handle selection offer from a client.
    void onSelectionOffer();

    /// Handle selection clear from a client.
    void onSelectionClear();

    // ── Clipboard history (Task 94) ─────────────────────────────────────

    /// Enable or disable clipboard history.
    void setHistoryEnabled(bool enabled) { m_historyEnabled = enabled; }

    /// Get clipboard history (newest first).
    [[nodiscard]] const std::vector<ClipboardEntry>& getHistory() const {
        return m_history;
    }

    /// Set maximum history entries.
    void setHistoryMaxSize(size_t maxSize) { m_historyMaxSize = maxSize; }

    /// Clear clipboard history.
    void clearHistory();

    /// Restore a history entry to the active clipboard.
    bool restoreFromHistory(size_t index);

    // ── Primary selection (Task 95) ─────────────────────────────────────

    /// Get the primary selection manager.
    [[nodiscard]] wlr_primary_selection_v1_device_manager*
    getPrimarySelectionManager() const {
        return m_primarySelectionManager;
    }

    /// Get the current primary selection MIME types.
    [[nodiscard]] std::vector<std::string> getPrimarySelectionMimeTypes() const;

    /// Read primary selection data for a specific MIME type.
    [[nodiscard]] std::vector<uint8_t> readPrimarySelection(
        const std::string& mimeType) const;

    /// Set primary selection data.
    bool setPrimarySelection(const std::vector<std::string>& mimeTypes,
                             const std::vector<uint8_t>& data);

    /// Clear the primary selection.
    void clearPrimarySelection();

    /// Handle middle-click paste of primary selection.
    void onMiddleClickPaste();

    /// Handle primary selection change per seat.
    void onPrimarySelectionChanged();

    // ── MIME negotiation ────────────────────────────────────────────────

    /// Check if the clipboard offers a specific MIME type.
    [[nodiscard]] bool hasMimeType(const std::string& mimeType) const;

    /// Negotiate the best MIME type from a list of preferred types.
    [[nodiscard]] std::string negotiateMimeType(
        const std::vector<std::string>& preferred) const;

private:
    /// Add a clipboard entry to history.
    void addToHistory(const ClipboardEntry& entry);

    Server& m_server;

    // wlroots clipboard protocols.
    wlr_data_device_manager* m_dataDeviceManager = nullptr;
    wlr_primary_selection_v1_device_manager* m_primarySelectionManager = nullptr;

    // Listeners.
    struct wl_listener m_selectionListener{};
    struct wl_listener m_primarySelectionListener{};

    // Clipboard history.
    bool m_historyEnabled = false;
    size_t m_historyMaxSize = 100;
    std::vector<ClipboardEntry> m_history;

    // Current MIME types available.
    std::vector<std::string> m_currentMimeTypes;
    std::vector<std::string> m_currentPrimaryMimeTypes;

    bool m_initialized = false;
};

} // namespace eternal
