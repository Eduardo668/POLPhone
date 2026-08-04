/* POLPhone - janela principal WinUI 3. GPL-2.0-only. */

#pragma once

#include "RingtoneService.h"
#include "core/PolPhoneController.h"
#include "core/Presentation.h"
#include "core/SettingsService.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/base.h>

#include <atomic>
#include <future>
#include <memory>
#include <string>

namespace polphone::gui {

class MainWindow final {
public:
    MainWindow(bool demoMode, const std::wstring& configPath);
    ~MainWindow();
    void activate();

private:
    using Border = winrt::Microsoft::UI::Xaml::Controls::Border;
    using Button = winrt::Microsoft::UI::Xaml::Controls::Button;
    using Grid = winrt::Microsoft::UI::Xaml::Controls::Grid;
    using TextBlock = winrt::Microsoft::UI::Xaml::Controls::TextBlock;

    struct UiLifetime {
        std::atomic<bool> closing{false};
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
    };

    void buildLayout();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildHeader();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildDialArea();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildCallArea();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildKeypad();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildSettingsPanel();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildDiagnosticsPanel();
    winrt::Microsoft::UI::Xaml::FrameworkElement buildDemoPanel();
    Button createButton(std::wstring_view text, bool primary = false,
                        bool destructive = false, double minWidth = 96.0);
    TextBlock createLabel(std::wstring_view text, double size = 14.0);
    void render(const core::TelephonySnapshot& snapshot);
    void run(std::future<util::Result<void>> operation);
    void showOperationError(const util::Error& error);
    void sendDigit(wchar_t digit);
    void startCall();
    void showOnly(Grid panel);
    void closePanels();
    void populateSettings(const core::GuiSettings& settings);
    void updateIncomingPresentation(const core::TelephonySnapshot& snapshot);

    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    std::shared_ptr<core::PolPhoneController> controller_;
    std::shared_ptr<UiLifetime> lifetime_;
    core::PhoneViewModel model_;
    RingtoneService ringtone_;
    bool demoMode_{false};
    bool topmostApplied_{false};
    std::wstring configPath_;

    Grid root_{nullptr};
    Grid overlay_{nullptr};
    Grid settingsPanel_{nullptr};
    Grid diagnosticsPanel_{nullptr};
    Grid dialPanel_{nullptr};
    Grid callPanel_{nullptr};
    Grid keypad_{nullptr};
    Border registrationDot_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox destination_{nullptr};
    TextBlock registrationText_{nullptr};
    TextBlock callStatusText_{nullptr};
    TextBlock remoteNameText_{nullptr};
    TextBlock remoteText_{nullptr};
    TextBlock durationText_{nullptr};
    TextBlock audioText_{nullptr};
    TextBlock statusText_{nullptr};
    TextBlock diagnosticsText_{nullptr};
    Button registerButton_{nullptr};
    Button callButton_{nullptr};
    Button answerButton_{nullptr};
    Button rejectButton_{nullptr};
    Button hangupButton_{nullptr};
    Button muteButton_{nullptr};
    Button keypadButton_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch ivrToggle_{nullptr};

    winrt::Microsoft::UI::Xaml::Controls::TextBox serverField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox registrarField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox idUriField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox displayNameField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox usernameField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox authUsernameField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBox domainField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::PasswordBox passwordField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch autoRegisterField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch ringtoneEnabledField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch topmostIncomingField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox captureField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox playbackField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox defaultDtmfField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::NumberBox settingsDurationField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::NumberBox settingsGapField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::NumberBox inbandVolumeField_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::NumberBox logLevelField_{nullptr};
};

} // namespace polphone::gui
