/**
 * @file        CAbort.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR AP compliant Abort handler functionality
 * @date        2025-11-03
 * @details     Implements ara::core Abort functionality according to AUTOSAR_AP_SWS_Core
 * @copyright   Copyright (c) 2025
 * @note        Fully compliant with AUTOSAR Adaptive Platform R23-11
 * 
 * @par AUTOSAR Specification References:
 * - SWS_CORE_00051: Abort function declaration
 * - SWS_CORE_00052: AbortHandler type definition
 * - SWS_CORE_00053: SetAbortHandler function
 * - SWS_CORE_00054: Abort behavior requirements
 * 
 * sdk:         AUTOSAR AP
 * platform:    Linux/POSIX
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * <tr><td>2025/11/03  <td>2.0      <td>ddkv587         <td>AUTOSAR AP compliant refactor
 * </table>
 */
#ifndef LAP_CORE_ABORT_HPP
#define LAP_CORE_ABORT_HPP

#include <csignal>
#include <atomic>
#include <cstdint>
#include "CTypedef.hpp"

namespace lap {
namespace core {

/**
 * @brief Prototype for custom abort handler function
 * @details This handler can be installed with SetAbortHandler. When Abort is called,
 *          the handler is invoked and may perform arbitrary operations.
 * 
 * @par Handler Behavior Options:
 * The handler has four principal choices for its final action:
 * - Terminate the process (recommended: call std::terminate() or std::_Exit())
 * - Return from the function (not recommended, may lead to undefined behavior)
 * - Enter an infinite loop (strongly discouraged)
 * - Perform non-local goto (std::longjmp) (strongly discouraged)
 * 
 * @par Thread Safety:
 * The handler may be invoked from any thread. It must be async-signal-safe if
 * used in signal handling contexts.
 * 
 * @par AUTOSAR Reference: SWS_CORE_00052
 */
void AbortHandlerPrototype() noexcept;

/**
 * @brief Type definition for abort handler function pointer
 * @details Defines the signature for custom abort handlers
 * 
 * @par AUTOSAR Reference: SWS_CORE_00052
 */
using AbortHandler = void (*)() noexcept;

/**
 * @brief Install a custom global abort handler
 * 
 * @param handler Pointer to the new abort handler function, or nullptr to restore default
 * @return The previously installed abort handler (may be nullptr)
 * 
 * @details This function atomically exchanges the current abort handler with the new one.
 *          By passing nullptr, the implementation restores the default handler.
 * 
 * @par Thread Safety:
 * This function can be called from multiple threads simultaneously. Calls are
 * serialized internally using atomic operations.
 * 
 * @par Exception Safety: No-throw guarantee
 * 
 * @par AUTOSAR Reference: SWS_CORE_00053
 * 
 * @code
 * void MyAbortHandler() noexcept {
 *     std::cerr << "Custom abort triggered!" << std::endl;
 *     std::terminate();
 * }
 * 
 * int main() {
 *     auto prev = SetAbortHandler(MyAbortHandler);
 *     // ... application logic ...
 *     SetAbortHandler(prev); // Restore previous handler
 * }
 * @endcode
 */
AbortHandler SetAbortHandler(AbortHandler handler) noexcept;

/**
 * @brief Get the currently installed abort handler
 * 
 * @return The current abort handler (may be nullptr if using default)
 * 
 * @details Returns the currently active abort handler without modifying it.
 * 
 * @par Thread Safety: Thread-safe (atomic load operation)
 * @par Exception Safety: No-throw guarantee
 * 
 * @since AUTOSAR AP R23-11 (extension)
 */
AbortHandler GetAbortHandler() noexcept;

/**
 * @brief Abort the current operation
 * 
 * @param text Descriptive text explaining the abort reason (may be nullptr)
 * 
 * @details This function will never return to its caller. The stack is not unwound:
 *          destructors of variables with automatic storage duration are not called.
 * 
 * @par Behavior:
 * 1. If text is non-null, it is logged to stderr (best-effort)
 * 2. The installed abort handler (if any) is invoked
 * 3. std::abort() is called to terminate the process
 * 
 * @par Thread Safety:
 * This function can be called from any thread. The abort handler and std::abort()
 * will be invoked in the calling thread's context.
 * 
 * @par Exception Safety: No-throw guarantee (function never returns)
 * 
 * @par AUTOSAR Reference: SWS_CORE_00051, SWS_CORE_00054
 * 
 * @warning This function terminates the program. It should only be used for
 *          unrecoverable errors where graceful shutdown is impossible.
 * 
 * @code
 * if (critical_error_detected) {
 *     Abort("Critical resource allocation failed");
 * }
 * @endcode
 */
[[noreturn]] void Abort(const Char* text) noexcept;

/**
 * @brief Abort without descriptive text
 * 
 * @details Convenience overload that calls Abort(nullptr)
 * 
 * @par AUTOSAR Reference: SWS_CORE_00051
 */
[[noreturn]] inline void Abort() noexcept {
    Abort(nullptr);
}

// ============================================================================
// Signal Handling Extensions (Platform-specific POSIX extensions)
// ============================================================================

/**
 * @brief Prototype for signal handler functions
 * @param signum Signal number being handled
 */
void SignalHandlerPrototype(int signum) noexcept;

/**
 * @brief Type definition for signal handler function pointer
 */
using SignalHandler = void (*)(int) noexcept;

/**
 * @brief Prototype for signal-specific custom handlers (no parameters)
 */
void SignalCustomHandlerPrototype() noexcept;

/**
 * @brief Type definition for signal-specific custom handler function pointer
 */
using SignalCustomHandler = void (*)() noexcept;

/**
 * @brief Register the default signal handler for common signals
 * 
 * @param handler The signal handler function (default: SignalHandlerPrototype)
 * 
 * @details Registers the handler for the following signals:
 * - SIGHUP, SIGINT, SIGQUIT, SIGABRT, SIGTERM (termination signals)
 * - SIGILL, SIGFPE, SIGSEGV (error signals)
 * 
 * @par Thread Safety: Not thread-safe (calls std::signal)
 * @par Exception Safety: No-throw guarantee
 */
// Use nullptr default to avoid creating an external symbol reference from callers.
// Implementation will substitute SignalHandlerPrototype when handler is nullptr.
void RegisterSignalHandler(SignalHandler handler = nullptr) noexcept;

/**
 * @brief Unregister all custom signal handlers, restoring defaults
 * 
 * @details Resets all signal handlers to SIG_DFL (default action)
 * 
 * @par Thread Safety: Not thread-safe (calls std::signal)
 * @par Exception Safety: No-throw guarantee
 */
void UnregisterSignalHandlers() noexcept;

// Per-signal custom handler setters
/**
 * @brief Set custom handler for SIGHUP signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGHUPHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGINT signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGINTHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGQUIT signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGQUITHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGABRT signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGABRTHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGFPE signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGFPEHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGILL signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGILLHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGSEGV signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGSEGVHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Set custom handler for SIGTERM signal
 * @param handler Custom handler or nullptr to disable
 * @return Previously installed handler
 */
SignalCustomHandler SetSignalSIGTERMHandler(SignalCustomHandler handler) noexcept;

/**
 * @brief Get the name string for a signal number
 * 
 * @param signum Signal number
 * @return Human-readable signal name (e.g., "SIGTERM" for SIGTERM)
 * 
 * @par Thread Safety: Thread-safe (returns const string)
 * @par Exception Safety: No-throw guarantee
 */
const Char* GetSignalName(int signum) noexcept;

/**
 * @brief Check if a signal handler is currently registered for a signal
 * 
 * @param signum Signal number to check
 * @return true if a custom handler is installed, false otherwise
 * 
 * @par Thread Safety: Thread-safe (atomic load)
 * @par Exception Safety: No-throw guarantee
 */
Bool IsSignalHandlerRegistered(int signum) noexcept;

} // namespace core
} // namespace lap

#endif // LAP_CORE_ABORT_HPP
