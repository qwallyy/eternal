#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace eternal {

// ---------------------------------------------------------------------------
// ConnectionID - opaque handle returned by Signal::connect
// ---------------------------------------------------------------------------

using ConnectionID = uint64_t;

// ---------------------------------------------------------------------------
// Signal<Args...> - a lightweight signal / slot mechanism
// ---------------------------------------------------------------------------

template <typename... Args>
class Signal {
public:
    Signal() = default;
    ~Signal() = default;

    // Non-copyable, movable
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) noexcept = default;
    Signal& operator=(Signal&&) noexcept = default;

    /// Connect a callback. Returns a ConnectionID that can be used to
    /// disconnect later.
    ConnectionID connect(std::function<void(Args...)> callback) {
        std::lock_guard lock(mutex_);
        ConnectionID id = next_id_++;
        slots_.emplace(id, std::move(callback));
        return id;
    }

    /// Disconnect a previously connected callback.
    void disconnect(ConnectionID id) {
        std::lock_guard lock(mutex_);
        slots_.erase(id);
    }

    /// Emit the signal, invoking every connected callback.
    void emit(Args... args) {
        // Copy slots under lock so callbacks may safely disconnect.
        decltype(slots_) copy;
        {
            std::lock_guard lock(mutex_);
            copy = slots_;
        }
        for (auto& [id, fn] : copy) {
            if (fn) fn(args...);
        }
    }

    /// Remove all connections.
    void clear() {
        std::lock_guard lock(mutex_);
        slots_.clear();
    }

    /// Number of active connections.
    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return slots_.size();
    }

private:
    mutable std::mutex mutex_;
    ConnectionID next_id_ = 1;
    std::unordered_map<ConnectionID, std::function<void(Args...)>> slots_;
};

// ---------------------------------------------------------------------------
// ConnectionGuard - RAII guard that disconnects on destruction
// ---------------------------------------------------------------------------

template <typename... Args>
class ConnectionGuard {
public:
    ConnectionGuard() = default;

    ConnectionGuard(Signal<Args...>& signal, ConnectionID id)
        : signal_(&signal), id_(id) {}

    ~ConnectionGuard() { disconnect(); }

    // Non-copyable, movable
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

    ConnectionGuard(ConnectionGuard&& other) noexcept
        : signal_(other.signal_), id_(other.id_) {
        other.signal_ = nullptr;
        other.id_ = 0;
    }

    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept {
        if (this != &other) {
            disconnect();
            signal_ = other.signal_;
            id_ = other.id_;
            other.signal_ = nullptr;
            other.id_ = 0;
        }
        return *this;
    }

    void disconnect() {
        if (signal_ && id_ != 0) {
            signal_->disconnect(id_);
            signal_ = nullptr;
            id_ = 0;
        }
    }

    [[nodiscard]] ConnectionID id() const noexcept { return id_; }

private:
    Signal<Args...>* signal_ = nullptr;
    ConnectionID id_ = 0;
};

} // namespace eternal
