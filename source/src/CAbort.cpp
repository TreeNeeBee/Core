/**
 * @file        CAbort.cpp
 * @brief       AUTOSAR AP compliant Abort handler implementation
 * @date        2025-11-03
 * @details     Implements ara::core Abort functionality with thread-safe signal handling
 */

#include "CAbort.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <unistd.h>

namespace lap {
namespace core {

// ============================================================================
// Internal Storage (namespace-scope atomic variables)
// ============================================================================

namespace {
    // Atomic storage for abort handler
    ::std::atomic<AbortHandler> g_abortHandler{nullptr};
    
    // Atomic storage for per-signal custom handlers
    ::std::atomic<SignalCustomHandler> g_signalHandlers[] = {
        {nullptr}, // SIGHUP
        {nullptr}, // SIGINT
        {nullptr}, // SIGQUIT
        {nullptr}, // SIGABRT
        {nullptr}, // SIGFPE
        {nullptr}, // SIGILL
        {nullptr}, // SIGSEGV
        {nullptr}, // SIGTERM
    };
    
    // Signal number to index mapping
    constexpr int SIGNAL_INDEX_SIGHUP = 0;
    constexpr int SIGNAL_INDEX_SIGINT = 1;
    constexpr int SIGNAL_INDEX_SIGQUIT = 2;
    constexpr int SIGNAL_INDEX_SIGABRT = 3;
    constexpr int SIGNAL_INDEX_SIGFPE = 4;
    constexpr int SIGNAL_INDEX_SIGILL = 5;
    constexpr int SIGNAL_INDEX_SIGSEGV = 6;
    constexpr int SIGNAL_INDEX_SIGTERM = 7;
    
    /**
     * @brief Map signal number to internal array index
     * @param signum Signal number
     * @return Array index, or -1 if signal not supported
     */
    int SignalToIndex(int signum) noexcept {
        switch (signum) {
            case SIGHUP:  return SIGNAL_INDEX_SIGHUP;
            case SIGINT:  return SIGNAL_INDEX_SIGINT;
            case SIGQUIT: return SIGNAL_INDEX_SIGQUIT;
            case SIGABRT: return SIGNAL_INDEX_SIGABRT;
            case SIGFPE:  return SIGNAL_INDEX_SIGFPE;
            case SIGILL:  return SIGNAL_INDEX_SIGILL;
            case SIGSEGV: return SIGNAL_INDEX_SIGSEGV;
            case SIGTERM: return SIGNAL_INDEX_SIGTERM;
            default:      return -1;
        }
    }
    
    /**
     * @brief Get signal-specific custom handler atomically
     * @param signum Signal number
     * @return Custom handler or nullptr
     */
    SignalCustomHandler GetSignalCustomHandler(int signum) noexcept {
        int idx = SignalToIndex(signum);
        if (idx < 0 || idx >= static_cast<int>(sizeof(g_signalHandlers) / sizeof(g_signalHandlers[0]))) {
            return nullptr;
        }
        return g_signalHandlers[idx].load(::std::memory_order_acquire);
    }
    
    /**
     * @brief Set signal-specific custom handler atomically
     * @param signum Signal number
     * @param handler New handler
     * @return Previous handler
     */
    SignalCustomHandler SetSignalCustomHandler(int signum, SignalCustomHandler handler) noexcept {
        int idx = SignalToIndex(signum);
        if (idx < 0 || idx >= static_cast<int>(sizeof(g_signalHandlers) / sizeof(g_signalHandlers[0]))) {
            return nullptr;
        }
        return g_signalHandlers[idx].exchange(handler, ::std::memory_order_acq_rel);
    }
    
    /**
     * @brief Signal name lookup table
     */
    struct SignalNameEntry {
        int signum;
        const char* name;
    };
    
    constexpr SignalNameEntry g_signalNames[] = {
        {SIGHUP,  "SIGHUP"},
        {SIGINT,  "SIGINT"},
        {SIGQUIT, "SIGQUIT"},
        {SIGABRT, "SIGABRT"},
        {SIGFPE,  "SIGFPE"},
        {SIGILL,  "SIGILL"},
        {SIGSEGV, "SIGSEGV"},
        {SIGTERM, "SIGTERM"},
    };
    
} // anonymous namespace

// ============================================================================
// Public API Implementation
// ============================================================================

void AbortHandlerPrototype() noexcept {
    // Empty prototype function - used for type definition only
}

void SignalCustomHandlerPrototype() noexcept {
    // Empty prototype function - used for type definition only
}

AbortHandler SetAbortHandler(AbortHandler handler) noexcept {
    return g_abortHandler.exchange(handler, ::std::memory_order_acq_rel);
}

AbortHandler GetAbortHandler() noexcept {
    return g_abortHandler.load(::std::memory_order_acquire);
}

[[noreturn]] void Abort(const Char* text) noexcept {
    // Best-effort logging to stderr (async-signal-safe write operations)
    if (text != nullptr && text[0] != '\0') {
        // Use write() for async-signal-safety instead of fprintf
        const char prefix[] = "[lap::core::Abort] ";
        ::write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
        ::write(STDERR_FILENO, text, ::strlen(text));
        ::write(STDERR_FILENO, "\n", 1);
    }
    
    // Invoke custom abort handler if registered
    AbortHandler handler = g_abortHandler.load(::std::memory_order_acquire);
    if (handler != nullptr) {
        handler();
    }
    
    // Terminate the process unconditionally
    ::std::abort();
    
    // The following line should never be reached, but satisfies [[noreturn]] attribute
    __builtin_unreachable();
}

// ============================================================================
// Signal Handling Implementation
// ============================================================================

void SignalHandlerPrototype(int signum) noexcept {
    // Get signal-specific custom handler
    SignalCustomHandler customHandler = GetSignalCustomHandler(signum);
    
    // Invoke custom handler if registered
    if (customHandler != nullptr) {
        customHandler();
    }
    
    // Restore default signal handler and re-raise
    ::std::signal(signum, SIG_DFL);
    ::std::raise(signum);
}

void RegisterSignalHandler(SignalHandler handler) noexcept {
    if (handler == nullptr) {
        handler = SignalHandlerPrototype;
    }
    ::std::signal(SIGHUP,  handler);
    ::std::signal(SIGINT,  handler);
    ::std::signal(SIGQUIT, handler);
    ::std::signal(SIGABRT, handler);
    ::std::signal(SIGFPE,  handler);
    ::std::signal(SIGILL,  handler);
    ::std::signal(SIGSEGV, handler);
    ::std::signal(SIGTERM, handler);
}

void UnregisterSignalHandlers() noexcept {
    ::std::signal(SIGHUP,  SIG_DFL);
    ::std::signal(SIGINT,  SIG_DFL);
    ::std::signal(SIGQUIT, SIG_DFL);
    ::std::signal(SIGABRT, SIG_DFL);
    ::std::signal(SIGFPE,  SIG_DFL);
    ::std::signal(SIGILL,  SIG_DFL);
    ::std::signal(SIGSEGV, SIG_DFL);
    ::std::signal(SIGTERM, SIG_DFL);
    
    // Clear all custom handlers
    for (auto& handler : g_signalHandlers) {
        handler.store(nullptr, ::std::memory_order_release);
    }
}

// Per-signal custom handler setters
SignalCustomHandler SetSignalSIGHUPHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGHUP, handler);
}

SignalCustomHandler SetSignalSIGINTHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGINT, handler);
}

SignalCustomHandler SetSignalSIGQUITHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGQUIT, handler);
}

SignalCustomHandler SetSignalSIGABRTHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGABRT, handler);
}

SignalCustomHandler SetSignalSIGFPEHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGFPE, handler);
}

SignalCustomHandler SetSignalSIGILLHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGILL, handler);
}

SignalCustomHandler SetSignalSIGSEGVHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGSEGV, handler);
}

SignalCustomHandler SetSignalSIGTERMHandler(SignalCustomHandler handler) noexcept {
    return SetSignalCustomHandler(SIGTERM, handler);
}

const Char* GetSignalName(int signum) noexcept {
    for (const auto& entry : g_signalNames) {
        if (entry.signum == signum) {
            return entry.name;
        }
    }
    return "UNKNOWN";
}

Bool IsSignalHandlerRegistered(int signum) noexcept {
    return GetSignalCustomHandler(signum) != nullptr;
}

} // namespace core
} // namespace lap
