/* POLPhone - ponto de entrada WinUI 3. GPL-2.0-only. */

#include "App.h"

#include <Windows.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/base.h>

#include <string>

namespace {

void checkXamlProcessRequirements()
{
    const HMODULE module = LoadLibraryW(L"Microsoft.UI.Xaml.dll");
    if (!module) {
        winrt::throw_last_error();
    }
    using CheckRequirements = void(WINAPI*)();
    const auto check = reinterpret_cast<CheckRequirements>(
        GetProcAddress(module, "XamlCheckProcessRequirements"));
    if (!check) {
        const DWORD error = GetLastError();
        FreeLibrary(module);
        winrt::throw_hresult(HRESULT_FROM_WIN32(error));
    }
    check();
    FreeLibrary(module);
}

void showStartupError(const wchar_t* message)
{
    MessageBoxW(
        nullptr,
        message,
        L"POLPhone — falha ao iniciar",
        MB_OK | MB_ICONERROR);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    bool demoMode = false;
    std::wstring configPath = L"config\\polphone.config.json";
    int count = 0;
    if (wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int index = 1; index < count; ++index) {
            const std::wstring argument = arguments[index];
            if (argument == L"--demo") demoMode = true;
            else if (argument == L"--config" && index + 1 < count) configPath = arguments[++index];
        }
        LocalFree(arguments);
    }
    wchar_t developmentDemo[8]{};
    if (GetEnvironmentVariableW(
            L"POLPHONE_DEMO", developmentDemo,
            static_cast<DWORD>(std::size(developmentDemo))) > 0
        && std::wstring_view(developmentDemo) == L"1") {
        demoMode = true;
    }

    try {
        checkXamlProcessRequirements();
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start(
            [demoMode, configPath](auto&&) {
                try {
                    winrt::make<polphone::gui::App>(demoMode, configPath);
                } catch (const winrt::hresult_error& error) {
                    showStartupError(error.message().c_str());
                } catch (const std::exception& error) {
                    const auto message = winrt::to_hstring(error.what());
                    showStartupError(message.c_str());
                }
            });
        return 0;
    } catch (const winrt::hresult_error& error) {
        showStartupError(error.message().c_str());
    } catch (const std::exception& error) {
        const auto message = winrt::to_hstring(error.what());
        showStartupError(message.c_str());
    }
    return 1;
}
