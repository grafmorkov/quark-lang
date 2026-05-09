#include <iostream>

#include "utils/logger.h"
#include "utils/colors.h"

using namespace utils::colors;
namespace utils::logger {

void print_location(const SourceLocation& loc) {
    if (loc.line >= 0) {
        std::cout << loc.file << ":" << loc.line << ":" << loc.column << " ";
    }
}
// diagnostics (WITH location)
void info(const SourceLocation& loc, const std::string& msg) {
    std::cout << "[info] ";
    print_location(loc);
    std::cout << msg << "\n";
}

void warn(const SourceLocation& loc, const std::string& msg) {
    set_color(YELLOW);
    std::cout << "[warn] ";
    print_location(loc);
    std::cout << msg << "\n";
    reset_color();
}

void error(const SourceLocation& loc, const std::string& msg) {
    set_color(RED);
    std::cerr << "[error] ";
    print_location(loc);
    std::cerr << msg << "\n";
    reset_color();
}

[[noreturn]] void fatal(const SourceLocation& loc, const std::string& msg) {
    error(loc, msg);
    std::exit(1);
}

// system logs (WITHOUT location)

void info(const std::string& msg) {
    std::cout << "[info] " << msg << "\n";
}

void warn(const std::string& msg) {
    set_color(YELLOW);
    std::cout << "[warn] " << msg << "\n"; 
    reset_color();
}

void error( const std::string& msg) {
    set_color(RED);
    std::cerr << "[error] " << msg << "\n";
    reset_color();
}

[[noreturn]] void fatal(const std::string& msg) {
    error(msg);
    std::exit(1);
}
[[noreturn]] void crash(const std::string& msg){
    throw std::runtime_error(msg);
}
[[noreturn]] void crash(const SourceLocation& loc, const std::string& msg) {
    std::ostringstream oss;

    if (loc.line >= 0) {
        oss << loc.file << ":" << loc.line << ":" << loc.column << " ";
    }
    oss << msg;

    throw std::runtime_error(oss.str());
}
}