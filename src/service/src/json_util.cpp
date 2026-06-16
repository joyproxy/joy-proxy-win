#include "json_util.h"

#include <sstream>

namespace joyproxy {

static size_t FindKey(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    return json.find(needle);
}

std::string JsonGetString(const std::string& json, const std::string& key) {
    const size_t pos = FindKey(json, key);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t colon = json.find(':', pos);
    if (colon == std::string::npos) {
        return {};
    }
    const size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) {
        return {};
    }
    std::string out;
    for (size_t i = q1 + 1; i < json.size(); ++i) {
        if (json[i] == '"') {
            break;
        }
        if (json[i] == '\\' && i + 1 < json.size()) {
            out.push_back(json[i + 1]);
            ++i;
        } else {
            out.push_back(json[i]);
        }
    }
    return out;
}

int JsonGetInt(const std::string& json, const std::string& key, int default_value) {
    const size_t pos = FindKey(json, key);
    if (pos == std::string::npos) {
        return default_value;
    }
    const size_t colon = json.find(':', pos);
    if (colon == std::string::npos) {
        return default_value;
    }
    try {
        size_t end = colon + 1;
        while (end < json.size() && (json[end] == ' ' || json[end] == '\t')) {
            ++end;
        }
        size_t stop = end;
        while (stop < json.size() && (json[stop] == '-' || isdigit(static_cast<unsigned char>(json[stop])))) {
            ++stop;
        }
        return std::stoi(json.substr(end, stop - end));
    } catch (...) {
        return default_value;
    }
}

std::string JsonEscape(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            default:
                oss << c;
                break;
        }
    }
    return oss.str();
}

std::string JsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out.push_back(s[i + 1]);
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

}  // namespace joyproxy
