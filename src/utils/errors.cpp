#include "utils/errors.h"

using namespace utils::colors;

void ErrorBag::add_source(const std::string& path, SourceFile sf) {
    sources[path] = std::move(sf);
}

void ErrorBag::print_carets(const SourceLocation& loc, int length, const std::string& line) {
    fprintf(stderr, "    %s\n", line.c_str());
    fprintf(stderr, "    ");

    for (int i = 0; i < loc.column - 1; i++) {
        fputc('~', stderr);
    }

    int length_to_use = (length > 0) ? length : 1;

    set_color(RED);
    for (int i = 0; i < length_to_use; i++) {
        fputc('^', stderr);
    }
    reset_color();

    int end_pos = loc.column - 1 + length_to_use;
    for (int i = end_pos; i < (int)line.size(); i++) {
        fputc('~', stderr);
    }
    fputc('\n', stderr);
}

void ErrorBag::add(const SourceLocation& loc, int length, const std::string& msg) {
    error_count++;

    set_color(RED);
    if (loc.line >= 0) {
        fprintf(stderr, "%s:%d:%d: error: %s\n",
                loc.file.c_str(), loc.line, loc.column, msg.c_str());
    } else {
        fprintf(stderr, "error: %s\n", msg.c_str());
    }
    reset_color();

    auto it = sources.find(loc.file);
    if (it != sources.end() && loc.line > 0 && (size_t)loc.line <= it->second.lines.size()) {
        const std::string& line = it->second.lines[loc.line - 1];
        print_carets(loc, length, line);
    }
}

void ErrorBag::add(const SourceLocation& loc, const std::string& msg) {
    add(loc, loc.length, msg);
}

void ErrorBag::add(const std::string& msg) {
    error_count++;
    set_color(RED);
    fprintf(stderr, "error: %s\n", msg.c_str());
    reset_color();
}
