#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <QString>
#define SPDLOG_STUB_WITH_QT

namespace spdlog
{
namespace detail
{
inline std::mutex &logger_mutex()
{
    static std::mutex m;
    return m;
}

inline std::string timestamp()
{
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto tt = clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

template <typename T>
inline void appendValue(std::ostringstream &oss, T &&value)
{
#ifdef SPDLOG_STUB_WITH_QT
    if constexpr (std::is_same_v<std::decay_t<T>, QString>) {
        oss << value.toStdString();
    } else
#endif
    {
        oss << std::forward<T>(value);
    }
}

inline void format_impl(std::ostringstream &oss, const std::string &fmt, size_t &pos)
{
    oss << fmt.substr(pos);
    pos = fmt.size();
}

template <typename T, typename... Args>
inline void format_impl(std::ostringstream &oss, const std::string &fmt, size_t &pos, T &&value, Args &&...args)
{
    size_t brace = fmt.find("{}", pos);
    if (brace == std::string::npos) {
        oss << fmt.substr(pos);
        pos = fmt.size();
        return;
    }
    oss << fmt.substr(pos, brace - pos);
    appendValue(oss, std::forward<T>(value));
    pos = brace + 2;
    format_impl(oss, fmt, pos, std::forward<Args>(args)...);
}

template <typename... Args>
inline std::string format(const std::string &fmt, Args &&...args)
{
    std::ostringstream oss;
    size_t pos = 0;
    format_impl(oss, fmt, pos, std::forward<Args>(args)...);
    if (pos < fmt.size()) {
        oss << fmt.substr(pos);
    }
    return oss.str();
}

inline void log_line(const char *level, const std::string &message)
{
    std::lock_guard<std::mutex> lock(logger_mutex());
    std::cerr << '[' << timestamp() << "] [" << level << "] " << message << std::endl;
}
} // namespace detail

inline void set_pattern(const std::string &)
{
    // Pattern customization not implemented in stub.
}

template <typename... Args>
inline void info(const std::string &fmt, Args &&...args)
{
    detail::log_line("info", detail::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void warn(const std::string &fmt, Args &&...args)
{
    detail::log_line("warn", detail::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void error(const std::string &fmt, Args &&...args)
{
    detail::log_line("error", detail::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void debug(const std::string &fmt, Args &&...args)
{
    detail::log_line("debug", detail::format(fmt, std::forward<Args>(args)...));
}
} // namespace spdlog
