#pragma once

#include <string>

namespace joyproxy {

std::string JsonGetString(const std::string& json, const std::string& key);
int JsonGetInt(const std::string& json, const std::string& key, int default_value = 0);
std::string JsonEscape(const std::string& s);
std::string JsonUnescape(const std::string& s);

}  // namespace joyproxy
