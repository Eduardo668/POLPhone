/* POLPhone - aplicação WinUI 3 C++/WinRT. GPL-2.0-only. */

#include "App.h"
#include "MainWindow.h"

#include <Windows.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/base.h>

#include <exception>
#include <utility>

namespace polphone::gui {

App::App(bool demoMode, std::wstring configPath)
    : demoMode_(demoMode), configPath_(std::move(configPath))
{
    UnhandledException([](
        auto&&,
        winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args) {
        MessageBoxW(
            nullptr,
            args.Message().c_str(),
            L"POLPhone — exceção não tratada pelo WinUI",
            MB_OK | MB_ICONERROR);
        args.Handled(true);
    });
}

void App::OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
{
    try {
        winrt::Microsoft::UI::Xaml::XamlTypeInfo::
            XamlControlsXamlMetaDataProvider::Initialize();
        metadataProvider_ = winrt::Microsoft::UI::Xaml::XamlTypeInfo::
            XamlControlsXamlMetaDataProvider();
        RequestedTheme(winrt::Microsoft::UI::Xaml::ApplicationTheme::Light);
        Resources().MergedDictionaries().Append(
            winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources());
        mainWindow_ = std::make_shared<MainWindow>(demoMode_, configPath_);
        mainWindow_->activate();
    } catch (const winrt::hresult_error& error) {
        MessageBoxW(
            nullptr,
            error.message().c_str(),
            L"POLPhone — erro de inicialização WinUI",
            MB_OK | MB_ICONERROR);
        Exit();
    } catch (const std::exception& error) {
        const auto message = winrt::to_hstring(error.what());
        MessageBoxW(
            nullptr,
            message.c_str(),
            L"POLPhone — erro de inicialização",
            MB_OK | MB_ICONERROR);
        Exit();
    }
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(
    winrt::Windows::UI::Xaml::Interop::TypeName const& type)
{
    return metadataProvider_.GetXamlType(type);
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(
    winrt::hstring const& fullName)
{
    return metadataProvider_.GetXamlType(fullName);
}

winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition>
App::GetXmlnsDefinitions()
{
    return metadataProvider_.GetXmlnsDefinitions();
}

} // namespace polphone::gui
