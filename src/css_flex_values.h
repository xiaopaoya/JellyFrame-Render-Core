#pragma once
#include <string>
namespace jellyframe::detail {
// Uses the same syntax parser as declaration application, without publishing Style.
bool supports_flex_value(const std::string& property,const std::string& value);
}
