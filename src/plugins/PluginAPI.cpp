#include "eternal/plugins/PluginAPI.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

// PluginAPI is a POD struct of function pointers defined in the header.
// This TU provides utility functions for working with PluginInfo and ConfigValue.

namespace eternal {

// ===========================================================================
// ConfigValue utilities
// ===========================================================================

std::string configValueToString(const ConfigValue& val) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << v;
            return oss.str();
        } else if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        }
        return "";
    }, val);
}

ConfigValue configValueFromString(const std::string& str, const std::string& type) {
    if (type == "int" || type == "integer") {
        try { return static_cast<int64_t>(std::stoll(str)); }
        catch (...) { return static_cast<int64_t>(0); }
    }
    if (type == "float" || type == "double") {
        try { return std::stod(str); }
        catch (...) { return 0.0; }
    }
    if (type == "bool" || type == "boolean") {
        return (str == "true" || str == "1" || str == "yes");
    }
    return str; // default: string
}

std::string configValueTypeName(const ConfigValue& val) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) return "int";
        else if constexpr (std::is_same_v<T, double>) return "float";
        else if constexpr (std::is_same_v<T, bool>) return "bool";
        else if constexpr (std::is_same_v<T, std::string>) return "string";
        return "unknown";
    }, val);
}

// ===========================================================================
// PluginInfo utilities
// ===========================================================================

std::string pluginInfoToJson(const PluginInfo& info) {
    std::string json = "{";
    json += R"("name":")" + info.name + R"(",)";
    json += R"("version":")" + info.version + R"(",)";
    json += R"("author":")" + info.author + R"(",)";
    json += R"("description":")" + info.description + R"(",)";

    uint32_t major = (info.api_version >> 16) & 0xFF;
    uint32_t minor = (info.api_version >> 8) & 0xFF;
    uint32_t patch = info.api_version & 0xFF;
    json += R"("api_version":")" + std::to_string(major) + "." +
            std::to_string(minor) + "." + std::to_string(patch) + R"(",)";

    json += R"("dependencies":[)";
    for (size_t i = 0; i < info.dependencies.size(); ++i) {
        if (i > 0) json += ",";
        json += R"(")" + info.dependencies[i] + R"(")";
    }
    json += "]}";
    return json;
}

bool isPluginAPICompatible(uint32_t plugin_version, uint32_t host_version) {
    uint32_t plugin_major = (plugin_version >> 16) & 0xFF;
    uint32_t host_major = (host_version >> 16) & 0xFF;

    // Major version must match
    if (plugin_major != host_major) return false;

    // Plugin minor must not exceed host minor
    uint32_t plugin_minor = (plugin_version >> 8) & 0xFF;
    uint32_t host_minor = (host_version >> 8) & 0xFF;
    if (plugin_minor > host_minor) return false;

    return true;
}

std::string formatAPIVersion(uint32_t version) {
    uint32_t major = (version >> 16) & 0xFF;
    uint32_t minor = (version >> 8) & 0xFF;
    uint32_t patch = version & 0xFF;
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

} // namespace eternal
