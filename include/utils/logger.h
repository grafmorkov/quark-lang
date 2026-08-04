#pragma once
#include <iostream>
#include <string_view>
#include <cstdlib>
#include <chrono>
#include <sstream>

namespace quanta{

    struct SourceLocation {
        std::string file = "<unknown>";
        int line = 0;
        int column = 0;
        int length = 0;
    };
}
using namespace quanta;

namespace utils::logger{
    // diagnostics (WITH location)
    void info(const SourceLocation& loc, const std::string& msg);
    void warn(const SourceLocation& loc, const std::string& msg);
    void error(const SourceLocation& loc, const std::string& msg);

    // system logs (WITHOUT location)
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

    [[noreturn]] void fatal(const std::string& msg);
    [[noreturn]] void fatal(const SourceLocation& loc, const std::string& msg);
    [[noreturn]] void crash(const std::string& msg);
    [[noreturn]] void crash(const SourceLocation& loc, const std::string& msg);
}
