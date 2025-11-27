#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

ConfigManager::ConfigManager() = default;

ConfigManager::~ConfigManager() = default;

bool ConfigManager::loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << configFile << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // 跳过注释行和空行
        if (isCommentOrEmpty(line)) {
            continue;
        }

        // 查找等号位置
        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos) {
            // 分割键和值
            std::string key = trim(line.substr(0, equalPos));
            std::string value = trim(line.substr(equalPos + 1));
            
            // 存储到map中
            configMap[key] = value;
        }
    }

    file.close();
    return true;
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = configMap.find(key);
    if (it != configMap.end()) {
        return it->second;
    }
    return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    auto it = configMap.find(key);
    if (it != configMap.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception& e) {
            std::cerr << "Failed to convert " << key << " to integer: " << it->second << std::endl;
        }
    }
    return defaultValue;
}

bool ConfigManager::getBool(const std::string& key, bool defaultValue) const {
    auto it = configMap.find(key);
    if (it != configMap.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        
        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            return true;
        } else if (value == "false" || value == "0" || value == "no" || value == "off") {
            return false;
        }
    }
    return defaultValue;
}

std::string ConfigManager::trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

bool ConfigManager::isCommentOrEmpty(const std::string& line) {
    std::string trimmedLine = trim(line);
    
    // 空行
    if (trimmedLine.empty()) {
        return true;
    }
    
    // 注释行 (# 或 ; 开头)
    if (trimmedLine[0] == '#' || trimmedLine[0] == ';') {
        return true;
    }
    
    return false;
}