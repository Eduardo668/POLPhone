/* POLPhone - recursos visuais centralizados da interface WinUI. GPL-2.0-only. */

#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.h>

namespace polphone::gui::theme {

// Fonte única da identidade POL. #0a3b68 = RGB(10, 59, 104).
inline constexpr winrt::Windows::UI::Color Primary{255, 10, 59, 104};
inline constexpr winrt::Windows::UI::Color PrimaryHover{255, 8, 49, 87};
inline constexpr winrt::Windows::UI::Color PrimaryPressed{255, 6, 39, 70};
inline constexpr winrt::Windows::UI::Color PrimarySoft{255, 232, 241, 248};
inline constexpr winrt::Windows::UI::Color Surface{255, 255, 255, 255};
inline constexpr winrt::Windows::UI::Color SurfaceMuted{255, 246, 248, 250};
inline constexpr winrt::Windows::UI::Color Overlay{190, 246, 248, 250};
inline constexpr winrt::Windows::UI::Color Text{255, 23, 43, 61};
inline constexpr winrt::Windows::UI::Color SubtleText{255, 74, 91, 107};
inline constexpr winrt::Windows::UI::Color Border{255, 201, 215, 227};
inline constexpr winrt::Windows::UI::Color Error{255, 176, 32, 37};
inline constexpr winrt::Windows::UI::Color ErrorHover{255, 148, 27, 31};
inline constexpr winrt::Windows::UI::Color ErrorPressed{255, 120, 22, 25};
inline constexpr winrt::Windows::UI::Color ErrorSoft{255, 253, 238, 238};
inline constexpr winrt::Windows::UI::Color Success{255, 24, 117, 75};

inline winrt::Microsoft::UI::Xaml::Media::SolidColorBrush brush(
    winrt::Windows::UI::Color color)
{
    return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(color);
}

inline void installAccentResources(
    const winrt::Microsoft::UI::Xaml::ResourceDictionary& resources)
{
    const auto putBrush = [&resources](const wchar_t* key, winrt::Windows::UI::Color color) {
        resources.Insert(winrt::box_value(winrt::hstring(key)), brush(color));
    };
    putBrush(L"AccentFillColorDefaultBrush", Primary);
    putBrush(L"AccentFillColorSecondaryBrush", PrimaryHover);
    putBrush(L"AccentFillColorTertiaryBrush", PrimaryPressed);
    putBrush(L"AccentFillColorDisabledBrush", PrimarySoft);
    putBrush(L"TextOnAccentFillColorPrimaryBrush", Surface);
}

} // namespace polphone::gui::theme
