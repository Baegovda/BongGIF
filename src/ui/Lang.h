#pragma once
// UI language. Korean strings are the keys; English is looked up
// at draw time in Gfx::text / textWidth, so every label goes through one place.
#include <string>

namespace bg {

void setLanguage(int lang);          // 0 Korean, 1 English
int language();
std::wstring tr(const std::wstring& ko);

} // namespace bg
