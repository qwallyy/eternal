#include "eternal/protocols/Clipboard.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_seat.h>
}

#include <algorithm>
#include <chrono>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Clipboard::Clipboard(Server& server)
    : m_server(server) {}

Clipboard::~Clipboard() {
    shutdown();
}

bool Clipboard::init() {
    auto* display = m_server.getDisplay();
    if (!display) {
        LOG_ERROR("Clipboard: no display available");
        return false;
    }

    // Create the wlr_data_device_manager (Task 94).
    m_dataDeviceManager = wlr_data_device_manager_create(display);
    if (!m_dataDeviceManager) {
        LOG_ERROR("Clipboard: failed to create data device manager");
        return false;
    }

    // Create the primary selection manager (Task 95).
    m_primarySelectionManager =
        wlr_primary_selection_v1_device_manager_create(display);
    if (!m_primarySelectionManager) {
        LOG_WARN("Clipboard: failed to create primary selection manager");
        // Non-fatal: primary selection is optional.
    }

    // Wire up selection change listeners.
    // These fire when a client sets a new clipboard/primary selection.
    m_selectionListener.notify = [](struct wl_listener* listener, void* data) {
        Clipboard* self = wl_container_of(listener, self, m_selectionListener);
        self->onSelectionOffer();
    };

    m_primarySelectionListener.notify = [](struct wl_listener* listener,
                                            void* data) {
        Clipboard* self = wl_container_of(listener, self,
                                           m_primarySelectionListener);
        self->onPrimarySelectionChanged();
    };

    // The seat selection signal notifies us when clipboard changes.
    // wl_signal_add(&seat->events.set_selection, &m_selectionListener);
    // wl_signal_add(&seat->events.set_primary_selection,
    //               &m_primarySelectionListener);

    m_initialized = true;
    LOG_INFO("Clipboard: initialized (data-device + primary selection)");
    return true;
}

void Clipboard::shutdown() {
    if (!m_initialized) return;

    // Listeners are cleaned up when the display is destroyed.
    m_history.clear();
    m_currentMimeTypes.clear();
    m_currentPrimaryMimeTypes.clear();
    m_initialized = false;

    LOG_INFO("Clipboard: shut down");
}

// ---------------------------------------------------------------------------
// wl_data_device clipboard (Task 94)
// ---------------------------------------------------------------------------

std::vector<std::string> Clipboard::getClipboardMimeTypes() const {
    return m_currentMimeTypes;
}

std::vector<uint8_t> Clipboard::readClipboard(
    const std::string& mimeType) const {
    // In a full implementation, this would:
    // 1. Get the current wlr_data_source from the seat
    // 2. Create a pipe
    // 3. Request the data source to send data for the MIME type
    // 4. Read from the pipe and return the data

    (void)mimeType;
    LOG_DEBUG("Clipboard: readClipboard for '{}'", mimeType);
    return {};
}

bool Clipboard::setClipboard(const std::vector<std::string>& mimeTypes,
                              const std::vector<uint8_t>& data) {
    // In a full implementation, this would create a wlr_data_source
    // that offers the given MIME types and data, then set it as the
    // seat's selection.

    m_currentMimeTypes = mimeTypes;

    // Add to history if enabled.
    if (m_historyEnabled && !data.empty()) {
        ClipboardEntry entry;
        entry.mimeTypes = mimeTypes;
        entry.data = data;
        entry.timestamp = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        addToHistory(entry);
    }

    LOG_DEBUG("Clipboard: set clipboard ({} MIME types, {} bytes)",
              mimeTypes.size(), data.size());
    return true;
}

void Clipboard::clearClipboard() {
    m_currentMimeTypes.clear();
    // wlr_seat_set_selection(seat, nullptr, serial);
    LOG_DEBUG("Clipboard: cleared");
}

void Clipboard::onSelectionOffer() {
    // Called when a client sets a new clipboard selection.
    // Update our cached MIME type list.
    // auto* source = seat->selection_source;
    // if (source) {
    //     m_currentMimeTypes = ... extract from source->mime_types ...;
    // }

    LOG_DEBUG("Clipboard: selection offer received");
}

void Clipboard::onSelectionClear() {
    m_currentMimeTypes.clear();
    LOG_DEBUG("Clipboard: selection cleared by client");
}

// ---------------------------------------------------------------------------
// Clipboard history (Task 94)
// ---------------------------------------------------------------------------

void Clipboard::clearHistory() {
    m_history.clear();
    LOG_DEBUG("Clipboard: history cleared");
}

bool Clipboard::restoreFromHistory(size_t index) {
    if (index >= m_history.size()) return false;

    const auto& entry = m_history[index];
    return setClipboard(entry.mimeTypes, entry.data);
}

void Clipboard::addToHistory(const ClipboardEntry& entry) {
    m_history.insert(m_history.begin(), entry);

    // Trim to max size.
    if (m_history.size() > m_historyMaxSize) {
        m_history.resize(m_historyMaxSize);
    }
}

// ---------------------------------------------------------------------------
// Primary selection (Task 95)
// ---------------------------------------------------------------------------

std::vector<std::string> Clipboard::getPrimarySelectionMimeTypes() const {
    return m_currentPrimaryMimeTypes;
}

std::vector<uint8_t> Clipboard::readPrimarySelection(
    const std::string& mimeType) const {
    // Similar to readClipboard but using the primary selection source.
    (void)mimeType;
    LOG_DEBUG("Clipboard: readPrimarySelection for '{}'", mimeType);
    return {};
}

bool Clipboard::setPrimarySelection(const std::vector<std::string>& mimeTypes,
                                     const std::vector<uint8_t>& data) {
    m_currentPrimaryMimeTypes = mimeTypes;
    // wlr_seat_set_primary_selection(seat, source, serial);

    LOG_DEBUG("Clipboard: set primary selection ({} MIME types)",
              mimeTypes.size());
    (void)data;
    return true;
}

void Clipboard::clearPrimarySelection() {
    m_currentPrimaryMimeTypes.clear();
    // wlr_seat_set_primary_selection(seat, nullptr, serial);
    LOG_DEBUG("Clipboard: primary selection cleared");
}

void Clipboard::onMiddleClickPaste() {
    // Triggered by middle mouse button press.
    // The compositor should paste the primary selection at the focused
    // surface's cursor position.
    LOG_DEBUG("Clipboard: middle-click paste triggered");

    // In a full implementation:
    // 1. Get the focused surface
    // 2. Get the primary selection data source
    // 3. Send the data to the surface via data_offer
}

void Clipboard::onPrimarySelectionChanged() {
    // Called when a client changes the primary selection (e.g. text highlight).
    LOG_DEBUG("Clipboard: primary selection changed");
}

// ---------------------------------------------------------------------------
// MIME negotiation
// ---------------------------------------------------------------------------

bool Clipboard::hasMimeType(const std::string& mimeType) const {
    return std::find(m_currentMimeTypes.begin(), m_currentMimeTypes.end(),
                     mimeType) != m_currentMimeTypes.end();
}

std::string Clipboard::negotiateMimeType(
    const std::vector<std::string>& preferred) const {
    // Return the first preferred MIME type that's available.
    for (const auto& pref : preferred) {
        if (hasMimeType(pref)) {
            return pref;
        }
    }

    // Fallback: return text/plain if available.
    if (hasMimeType("text/plain;charset=utf-8")) {
        return "text/plain;charset=utf-8";
    }
    if (hasMimeType("text/plain")) {
        return "text/plain";
    }

    // Return the first available type.
    return m_currentMimeTypes.empty() ? "" : m_currentMimeTypes.front();
}

} // namespace eternal
