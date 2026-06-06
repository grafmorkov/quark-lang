#include "utils/file_manager.h"

#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

namespace utils::io{
    std::string read_file(const std::filesystem::path& filePath) {
        std::ifstream file(filePath, std::ios::binary);

        if (!file)
            logger::fatal("Failed to read file: " + filePath.string());

        file.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::string content(size, '\0');
        file.read(content.data(), size);

        return content;
    }

    std::filesystem::path get_executable_directory() {
#if defined(_WIN32)
        wchar_t path[MAX_PATH];
        DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
        if (length == 0) {
            return "";
        }
        return fs::path(path).parent_path();

#elif defined(__linux__)
        char path[PATH_MAX];
        ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (length == -1) {
            return "";
        }
        path[length] = '\0'; // Null-terminate string
        return std::filesystem::path(path).parent_path();

#elif defined(__APPLE__)
        char path[PATH_MAX];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) != 0) {
            // Buffer was too small; resize dynamically
            std::vector<char> dynamic_path(size);
            if (_NSGetExecutablePath(dynamic_path.data(), &size) == 0) {
                return fs::canonical(fs::path(dynamic_path.data())).parent_path();
            }
            return "";
        }
        return fs::canonical(fs::path(path)).parent_path();

#else
        return ""; // Unsupported platform
#endif
    }
}