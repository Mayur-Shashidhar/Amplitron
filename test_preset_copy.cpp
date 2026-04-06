#include "src/preset_manager.h"
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace Amplitron;

int main() {
    std::string test_dir = "/tmp/test_presets_copy";
    
    // Clean up if exists
    std::filesystem::remove_all(test_dir);
    
    std::cout << "Before set_presets_dir:" << std::endl;
    std::cout << "  test_dir exists: " << std::filesystem::exists(test_dir) << std::endl;
    
    // Call set_presets_dir
    std::cout << "\nCalling PresetManager::set_presets_dir(\"" << test_dir << "\")..." << std::endl;
    PresetManager::set_presets_dir(test_dir);
    
    std::cout << "\nAfter set_presets_dir:" << std::endl;
    std::cout << "  test_dir exists: " << std::filesystem::exists(test_dir) << std::endl;
    
    if (std::filesystem::exists(test_dir)) {
        int file_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::cout << "  - " << entry.path().filename().string() << std::endl;
                file_count++;
            }
        }
        std::cout << "  Total JSON files: " << file_count << std::endl;
    }
    
    // Cleanup
    PresetManager::set_presets_dir("");
    std::filesystem::remove_all(test_dir);
    
    return 0;
}
