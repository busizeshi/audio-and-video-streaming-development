#ifndef RTSP_PUBLISH_CONFIGMANAGER_H
#define RTSP_PUBLISH_CONFIGMANAGER_H

#include <string>
#include <unordered_map>

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    // 加载配置文件
    bool loadConfig(const std::string& configFile);
    
    // 获取字符串值
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    
    // 获取整数值
    int getInt(const std::string& key, int defaultValue = 0) const;
    
    // 获取布尔值
    bool getBool(const std::string& key, bool defaultValue = false) const;

private:
    std::unordered_map<std::string, std::string> configMap;
    
    // 去除字符串两端空格
    std::string trim(const std::string& str) const;
    
    // 判断是否为注释行或空行
    bool isCommentOrEmpty(const std::string& line) const;
};

#endif //RTSP_PUBLISH_CONFIGMANAGER_H