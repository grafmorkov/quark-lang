#pragma once
#include <iostream>

namespace utils::colors {

#ifdef _WIN32
#include <windows.h>

inline void set_color(WORD color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color | FOREGROUND_INTENSITY);
}

inline void reset_color() {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

// Color codes
constexpr WORD BLACK   = 0;
constexpr WORD BLUE    = FOREGROUND_BLUE;
constexpr WORD GREEN   = FOREGROUND_GREEN;
constexpr WORD CYAN    = FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr WORD RED     = FOREGROUND_RED;
constexpr WORD MAGENTA = FOREGROUND_RED | FOREGROUND_BLUE;
constexpr WORD YELLOW  = FOREGROUND_RED | FOREGROUND_GREEN;
constexpr WORD WHITE   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

#else
// Unix-like ANSI escape codes
inline void set_color(const char* color_code) {
    std::cout << "\033[" << color_code << "m";
}

inline void reset_color() {
    std::cout << "\033[0m";
}

// Color codes
constexpr const char* BLACK   = "0;30";
constexpr const char* RED     = "0;31";
constexpr const char* GREEN   = "0;32";
constexpr const char* YELLOW  = "0;33";
constexpr const char* BLUE    = "0;34";
constexpr const char* MAGENTA = "0;35";
constexpr const char* CYAN    = "0;36";
constexpr const char* WHITE   = "0;37";
constexpr const char* BRIGHT  = "1";

#endif

} // namespace quark::colors
