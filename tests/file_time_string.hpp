#pragma once

// How Catch2 shows a file time when a comparison of two fails. Its own
// chrono printing cannot do it on libc++ (macOS), which counts file times in a
// 128-bit integer that no stream operator takes: a CHECK comparing two file
// times would not compile there without this.

#include <catch2/catch_tostring.hpp>

#include <chrono>
#include <filesystem>
#include <string>

template <>
struct Catch::StringMaker<std::filesystem::file_time_type> {
    static std::string convert(const std::filesystem::file_time_type& time)
    {
        return std::to_string(
                   std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count())
            + " ns";
    }
};
