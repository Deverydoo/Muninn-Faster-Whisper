#pragma once

#include "export.h"
#include <string>
#include <functional>
#include <sstream>
#include <iostream>
#include <fstream>
#include <mutex>

namespace muninn {

/**
 * @brief Log level enumeration
 */
enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

/**
 * @brief Logging callback type
 *
 * @param level Log level (debug, info, warning, error)
 * @param message The log message
 */
using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

/**
 * @brief Global logging configuration for Muninn
 *
 * This allows the host application (e.g., Loki Studio) to redirect
 * Muninn's logs to its own logging system instead of stdout/stderr.
 *
 * Additionally, file logging can be enabled by calling set_log_directory().
 * File logging works independently of the callback - both can be active.
 */
class MUNINN_API Logger {
public:
    /**
     * @brief Set a custom logging callback
     *
     * When set, all Muninn log output goes to this callback instead of stdout/stderr.
     * Set to nullptr to restore default stdout/stderr behavior.
     *
     * @param callback The logging callback function
     */
    static void set_callback(LogCallback callback);

    /**
     * @brief Get the current logging callback
     * @return The current callback, or nullptr if using default logging
     */
    static LogCallback get_callback();

    /**
     * @brief Enable file logging to a directory
     *
     * Creates a muninn.log file in the specified directory.
     * The log file is appended to on each session, with session markers.
     * File logging works in addition to the callback (if set).
     *
     * @param directory Path to the logs directory (e.g., "C:/Users/user/AppData/Local/LokiStudio/logs")
     * @return true if file logging was successfully enabled
     */
    static bool set_log_directory(const std::string& directory);

    /**
     * @brief Disable file logging
     */
    static void disable_file_logging();

    /**
     * @brief Check if file logging is enabled
     * @return true if logging to file
     */
    static bool is_file_logging_enabled();

    /**
     * @brief Log a debug message
     */
    static void debug(const std::string& message);

    /**
     * @brief Log an info message
     */
    static void info(const std::string& message);

    /**
     * @brief Log a warning message
     */
    static void warn(const std::string& message);

    /**
     * @brief Log an error message
     */
    static void error(const std::string& message);

    /**
     * @brief Internal logging function
     */
    static void log(LogLevel level, const std::string& message);

private:
    static LogCallback callback_;
    static std::ofstream log_file_;
    static std::mutex file_mutex_;
    static bool file_logging_enabled_;
    static std::string log_directory_;

    static void write_to_file(LogLevel level, const std::string& message);
    static std::string get_timestamp();
    static std::string level_to_string(LogLevel level);
};

// Convenience macros for logging with automatic [Muninn] prefix
#define MUNINN_LOG_DEBUG(msg) do { \
    std::ostringstream oss; oss << msg; \
    muninn::Logger::debug(oss.str()); \
} while(0)

#define MUNINN_LOG_INFO(msg) do { \
    std::ostringstream oss; oss << msg; \
    muninn::Logger::info(oss.str()); \
} while(0)

#define MUNINN_LOG_WARN(msg) do { \
    std::ostringstream oss; oss << msg; \
    muninn::Logger::warn(oss.str()); \
} while(0)

#define MUNINN_LOG_ERROR(msg) do { \
    std::ostringstream oss; oss << msg; \
    muninn::Logger::error(oss.str()); \
} while(0)

} // namespace muninn
