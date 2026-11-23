#pragma once

#include <cstdarg>
#include <cstring>
#include <iostream>

#define LOG_INFO(fmt, ...)                                                     \
  componeng::utils::Logger::Log(__FILE__, __FUNCTION__, "INFO",                \
                                fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                                    \
  componeng::utils::Logger::Log(__FILE__, __FUNCTION__, "ERROR",               \
                                fmt, ##__VA_ARGS__)

namespace componeng::utils {

/**
 * @brief Minimal printf-style logger that prints timestamped messages to
 * stdout.
 *
 * Used via LOG_INFO and LOG_ERROR macros which automatically inject
 * the source file and function name.
 * Thread-unsafe (intended for single-threaded debug output).
 */
class Logger {
public:
  // format(printf, 4, 5): lets -Wformat check LOG_INFO/LOG_ERROR call sites
  __attribute__((format(printf, 4, 5))) static void Log(const char *file,
                                                        const char *func,
                                                        const char *prefix,
                                                        const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char *filename = strrchr(file, '/');
    if (filename)
      file = filename + 1;

    std::string finalMsg;
    if (prefix[0] != '\0') {
      finalMsg =
          std::string("[") + prefix + "] [" + file + ":" + func + "] " + buffer;
    } else {
      finalMsg = std::string("[") + file + ":" + func + "] " + buffer;
    }

    std::cout << finalMsg << std::endl;
  }
};

} // namespace componeng::utils
