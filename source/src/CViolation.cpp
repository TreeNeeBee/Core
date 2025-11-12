/**
 * @file        CViolation.cpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Implementation of AUTOSAR-compliant violation handling
 * @date        2025-11-12
 */

#include "CViolation.hpp"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

namespace lap
{
namespace core
{
    const Char* ViolationTypeToString(ViolationType type) noexcept
    {
        switch (type)
        {
            case ViolationType::kPlatformNotInitialized:
                return "PlatformNotInitialized";
            case ViolationType::kInvalidArgument:
                return "InvalidArgument";
            case ViolationType::kConfigurationMissing:
                return "ConfigurationMissing";
            case ViolationType::kResourceExhausted:
                return "ResourceExhausted";
            case ViolationType::kStateCorruption:
                return "StateCorruption";
            case ViolationType::kExternalSystemFailure:
                return "ExternalSystemFailure";
            case ViolationType::kSecurityViolation:
                return "SecurityViolation";
            case ViolationType::kAssertionFailure:
                return "AssertionFailure";
            default:
                return "UnknownViolation";
        }
    }
    
    [[noreturn]] void RaiseViolation(
        ViolationType type,
        const Char* message,
        const Char* file,
        UInt32 line
    ) noexcept
    {
        // Use async-signal-safe functions only (per POSIX)
        // This ensures RaiseViolation can be called from signal handlers
        
        // Get current timestamp
        time_t now = std::time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        // Get process ID
        pid_t pid = getpid();
        
        // Write violation header
        const char* header = "\n"
            "================================================================================\n"
            "                        AUTOSAR VIOLATION DETECTED\n"
            "================================================================================\n";
        std::fputs(header, stderr);
        
        // Write timestamp and process info
        std::fprintf(stderr, "Time:    %s\n", timestamp);
        std::fprintf(stderr, "PID:     %d\n", static_cast<int>(pid));
        
        // Write violation type
        std::fprintf(stderr, "Type:    %s\n", ViolationTypeToString(type));
        
        // Write message
        if (message) {
            std::fprintf(stderr, "Message: %s\n", message);
        }
        
        // Write source location if available
        if (file) {
            std::fprintf(stderr, "File:    %s", file);
            if (line > 0) {
                std::fprintf(stderr, ":%u", line);
            }
            std::fprintf(stderr, "\n");
        }
        
        // Write footer and guidance
        const char* footer = 
            "--------------------------------------------------------------------------------\n"
            "Per AUTOSAR [SWS_CORE_00021], this violation is non-recoverable.\n"
            "The process will now terminate via std::abort().\n"
            "================================================================================\n\n";
        std::fputs(footer, stderr);
        
        std::fflush(stderr);
        
        // Terminate process per [SWS_CORE_00003]
        std::abort();
    }

} // namespace core
} // namespace lap
