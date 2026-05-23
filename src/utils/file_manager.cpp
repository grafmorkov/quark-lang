#include "utils/file_manager.h"

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
}