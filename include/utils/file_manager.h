#pragma once

#include <iostream>
#include <string>
#include <fstream> 
#include <filesystem>

#include "utils/logger.h"

namespace utils::io{
    std::string read_file(const std::filesystem::path& filePath);
    std::filesystem::path get_executable_directory();
}
