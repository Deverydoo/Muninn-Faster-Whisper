/**
 * @file logging.cpp
 * @brief Implementation of Muninn logging system
 */

#include "muninn/logging.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_portable(path) _mkdir(path)
#else
#include <sys/stat.h>
#define mkdir_portable(path) mkdir(path, 0755)
#endif

namespace muninn {

namespace {

// The callback is held through a shared_ptr: log() copies the pointer under
// the lock and calls it outside, so set_callback() can swap it while other
// threads are mid-call, and a callback that logs doesn't deadlock.
std::mutex g_callback_mutex;
std::shared_ptr<const LogCallback> g_callback;

std::mutex g_file_mutex;
std::ofstream g_log_file;
std::string g_log_directory;
std::atomic<bool> g_file_logging_enabled{false};

// One whole line per write, so lines from different threads never mix.
std::mutex g_console_mutex;

std::shared_ptr<const LogCallback> current_callback() {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    return g_callback;
}

} // namespace

void Logger::set_callback(LogCallback callback) {
    std::shared_ptr<const LogCallback> next;
    if (callback) next = std::make_shared<const LogCallback>(std::move(callback));
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    g_callback = std::move(next);
}

LogCallback Logger::get_callback() {
    auto cb = current_callback();
    return cb ? *cb : LogCallback();
}

bool Logger::set_log_directory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(g_file_mutex);

    // Close existing file if open
    if (g_log_file.is_open()) {
        g_log_file.close();
    }

    // Create directory if it doesn't exist (best effort)
    mkdir_portable(directory.c_str());

    // Build log file path
    std::string log_path = directory;
    if (!log_path.empty() && log_path.back() != '/' && log_path.back() != '\\') {
        log_path += "/";
    }
    log_path += "muninn.log";

    // Open file in append mode
    g_log_file.open(log_path, std::ios::app);
    if (!g_log_file.is_open()) {
        g_file_logging_enabled.store(false);
        std::lock_guard<std::mutex> console(g_console_mutex);
        std::cerr << "[Muninn] Failed to open log file: " << log_path << "\n";
        return false;
    }

    g_log_directory = directory;

    // Write session header
    g_log_file << "\n";
    g_log_file << "═══════════════════════════════════════════════════════════\n";
    g_log_file << "  Muninn Transcription Engine - Session Started\n";
    g_log_file << "  " << get_timestamp() << "\n";
    g_log_file << "═══════════════════════════════════════════════════════════\n";
    g_log_file.flush();

    g_file_logging_enabled.store(true);
    return true;
}

void Logger::disable_file_logging() {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    g_file_logging_enabled.store(false);
    if (g_log_file.is_open()) {
        g_log_file << "[" << get_timestamp() << "] Session ended\n";
        g_log_file.close();
    }
    g_log_directory.clear();
}

bool Logger::is_file_logging_enabled() {
    return g_file_logging_enabled.load();
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    // Always write to file if enabled
    if (g_file_logging_enabled.load(std::memory_order_relaxed)) {
        write_to_file(level, message);
    }

    if (auto cb = current_callback()) {
        (*cb)(level, message);
        return;
    }

    // Default: stderr at every level. stdout belongs to the host (Loki
    // Studio's worker speaks JSON on it).
    const char* tag = "";
    switch (level) {
        case LogLevel::Debug:   tag = "[DEBUG] "; break;
        case LogLevel::Info:    tag = ""; break;
        case LogLevel::Warning: tag = "[WARN] "; break;
        case LogLevel::Error:   tag = "[ERROR] "; break;
    }
    std::string line = "[Muninn] ";
    line += tag;
    line += message;
    line += '\n';

    std::lock_guard<std::mutex> lock(g_console_mutex);
    std::cerr.write(line.data(), static_cast<std::streamsize>(line.size()));
    std::cerr.flush();
}

void Logger::write_to_file(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    if (!g_log_file.is_open()) return;

    g_log_file << "[" << get_timestamp() << "] "
               << "[" << level_to_string(level) << "] "
               << message << "\n";
    g_log_file.flush();
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "???";
    }
}

} // namespace muninn
