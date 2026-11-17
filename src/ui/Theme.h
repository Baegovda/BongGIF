#pragma once
// BongHub design tokens (dark). Every colour/size used by the UI comes from here.
// Colours are ARGB (0xAARRGGBB).
#include <cstdint>

namespace bg::th {

// ---- colour
constexpr uint32_t background = 0xFF0B0D12;
constexpr uint32_t textPrimary = 0xFFF5F7FA;
constexpr uint32_t textSecondary = 0xFFA3ABBC;
constexpr uint32_t textTertiary = 0xFF6E7686;
constexpr uint32_t accent = 0xFF4C8AFE;
constexpr uint32_t accentEnd = 0xFFA98BFF;          // accent.gradient left -> right
constexpr uint32_t accentEndAlt = 0xFFE47EF4;
constexpr uint32_t cardTop = 0x2CFFFFFF, cardBottom = 0x16FFFFFF;
constexpr uint32_t strokeTop = 0x44FFFFFF, strokeBottom = 0x14FFFFFF;
constexpr uint32_t separator = 0x1FFFFFFF;
constexpr uint32_t control = 0x26FFFFFF, controlHover = 0x38FFFFFF;
constexpr uint32_t menuBackground = 0xF0202430, menuStroke = 0x38FFFFFF, menuHover = 0x3A4B6EFF;
constexpr uint32_t green = 0xFF3DDC97, cyan = 0xFF31D0E0, purple = 0xFFA98BFF, pink = 0xFFFF7AB8,
                   orange = 0xFFFF9F43, yellow = 0xFFFFD84D, red = 0xFFFF5D5D;
// derived (still only from the tokens above, with alpha)
constexpr uint32_t sidebar = 0x0CFFFFFF;            // faint glass for the side bar
constexpr uint32_t canvasWell = 0x66000000;         // image well inside the canvas card
constexpr uint32_t shadow = 0x000000;               // rgb only; alpha comes from shadowOpacity
constexpr float shadowOpacity = 0.25f;

// ---- type (DIPs)
constexpr const wchar_t* fontBody = L"Segoe UI Variable Text";
constexpr const wchar_t* fontDisplay = L"Segoe UI Variable Display";
constexpr const wchar_t* fontIcons = L"Segoe Fluent Icons";
constexpr const wchar_t* fontIconsFallback = L"Segoe MDL2 Assets";
constexpr float pageTitle = 22, sectionTitle = 16, body = 13.5f, caption = 12;

// ---- shape / spacing (DIPs)
constexpr float radiusCard = 10, radiusPanel = 14, radiusChip = 5, radiusControl = 6;
constexpr float grid = 4, cardPadding = 16, cardGap = 10;
constexpr float rowHeight = 32, controlHeight = 30, titleBarHeight = 44;
constexpr float shadowBlur = 20, shadowOffsetY = 3;

// ---- motion (ms)
constexpr float hoverMs = 150, toggleMs = 150, popupInMs = 180, popupOutMs = 120, popupRise = 8;

} // namespace bg::th
