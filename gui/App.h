/* POLPhone - aplicação WinUI 3 C++/WinRT. GPL-2.0-only. */

#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <memory>
#include <string>

namespace polphone::gui {

class MainWindow;

struct App : winrt::Microsoft::UI::Xaml::ApplicationT<
                 App,
                 winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {
    App(bool demoMode, std::wstring configPath);
    void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(
        winrt::Windows::UI::Xaml::Interop::TypeName const& type);
    winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(
        winrt::hstring const& fullName);
    winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition>
        GetXmlnsDefinitions();

private:
    bool demoMode_{false};
    std::wstring configPath_;
    std::shared_ptr<MainWindow> mainWindow_;
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider
        metadataProvider_{nullptr};
};

} // namespace polphone::gui
