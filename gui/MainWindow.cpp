/* POLPhone - janela compacta WinUI 3. GPL-2.0-only. */

#include "MainWindow.h"

#include "Theme.h"
#include "core/ControllerFactory.h"
#include "core/SettingsService.h"

#include <Windows.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace polphone::gui {
namespace {

GridLength automatic() { return GridLength{0, GridUnitType::Auto}; }
GridLength star() { return GridLength{1, GridUnitType::Star}; }

Border card(UIElement const& content, double padding = 18.0)
{
    Border border;
    border.Background(theme::brush(theme::Surface));
    border.BorderBrush(theme::brush(theme::Border));
    border.BorderThickness(Thickness{1, 1, 1, 1});
    border.CornerRadius(CornerRadius{10, 10, 10, 10});
    border.Padding(Thickness{padding, padding, padding, padding});
    border.Child(content);
    return border;
}

void tooltip(const DependencyObject& target, std::wstring_view text)
{
    ToolTipService::SetToolTip(target, box_value(hstring(text)));
    AutomationProperties::SetName(target, hstring(text));
}

bool isIdleLike(core::CallState state) noexcept
{
    return state == core::CallState::Idle || state == core::CallState::Disconnected
        || state == core::CallState::Error;
}

} // namespace

MainWindow::MainWindow(bool demoMode, const std::wstring& configPath)
    : window_(Window()), lifetime_(std::make_shared<UiLifetime>()), demoMode_(demoMode),
      configPath_(configPath)
{
    lifetime_->dispatcher = window_.DispatcherQueue();
    controller_ = demoMode
        ? std::shared_ptr<core::PolPhoneController>(core::createDemoController().release())
        : std::shared_ptr<core::PolPhoneController>(
            core::createRealController(std::filesystem::path(configPath)).release());

    window_.Title(L"POLPhone");
    buildLayout();
    if (!demoMode_) {
        const auto settings = core::loadGuiSettings(std::filesystem::path(configPath_));
        if (settings) populateSettings(settings.value());
    }
    controller_->setStateChangedHandler(
        [lifetime = lifetime_, this](const core::TelephonySnapshot& snapshot) {
            if (lifetime->closing.load()) return;
            lifetime->dispatcher.TryEnqueue([lifetime, this, snapshot] {
                if (!lifetime->closing.load()) render(snapshot);
            });
        });
    window_.Closed([this](auto&&, auto&&) {
        lifetime_->closing = true;
        ringtone_.stop();
        controller_->setStateChangedHandler({});
        static_cast<void>(controller_->shutdown());
    });
    render(controller_->getState());
    run(controller_->initialize());
}

MainWindow::~MainWindow()
{
    ringtone_.stop();
    lifetime_->closing = true;
    if (controller_) controller_->setStateChangedHandler({});
}

void MainWindow::activate()
{
    window_.Activate();
    HWND hwnd{};
    if (SUCCEEDED(window_.as<::IWindowNative>()->get_WindowHandle(&hwnd)) && hwnd != nullptr) {
        const UINT dpi = GetDpiForWindow(hwnd);
        const int width = MulDiv(452, static_cast<int>(dpi), 96);
        const int height = MulDiv(690, static_cast<int>(dpi), 96);
        SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

TextBlock MainWindow::createLabel(std::wstring_view text, double size)
{
    TextBlock label;
    label.Text(hstring(text));
    label.FontSize(size);
    label.Foreground(theme::brush(theme::Text));
    label.TextWrapping(TextWrapping::Wrap);
    return label;
}

MainWindow::Button MainWindow::createButton(
    std::wstring_view text, bool primary, bool destructive, double minWidth)
{
    Button button;
    button.Content(box_value(hstring(text)));
    button.MinWidth(minWidth);
    button.MinHeight(42);
    button.Padding(Thickness{14, 8, 14, 8});
    button.CornerRadius(CornerRadius{7, 7, 7, 7});
    const auto fill = destructive ? theme::Error : (primary ? theme::Primary : theme::Surface);
    button.Background(theme::brush(fill));
    button.Foreground(theme::brush(primary || destructive ? theme::Surface : theme::Primary));
    button.BorderBrush(theme::brush(destructive ? theme::Error : theme::Primary));
    button.BorderThickness(Thickness{1, 1, 1, 1});
    AutomationProperties::SetName(button, hstring(text));
    return button;
}

void MainWindow::buildLayout()
{
    root_ = Grid();
    root_.Background(theme::brush(theme::SurfaceMuted));
    root_.MinWidth(400);
    root_.RowDefinitions().Append(RowDefinition());
    root_.RowDefinitions().GetAt(0).Height(automatic());
    root_.RowDefinitions().Append(RowDefinition());
    root_.RowDefinitions().GetAt(1).Height(star());
    root_.RowDefinitions().Append(RowDefinition());
    root_.RowDefinitions().GetAt(2).Height(automatic());

    auto header = buildHeader();
    Grid::SetRow(header, 0);
    root_.Children().Append(header);

    ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    StackPanel body;
    body.Spacing(12);
    body.Padding(Thickness{16, 14, 16, 14});
    body.MaxWidth(480);
    body.HorizontalAlignment(HorizontalAlignment::Stretch);
    dialPanel_ = buildDialArea().as<Grid>();
    callPanel_ = buildCallArea().as<Grid>();
    body.Children().Append(dialPanel_);
    body.Children().Append(callPanel_);
    body.Children().Append(buildKeypad());
    if (demoMode_) body.Children().Append(buildDemoPanel());
    scroll.Content(body);
    Grid::SetRow(scroll, 1);
    root_.Children().Append(scroll);

    Border footer;
    footer.Background(theme::brush(theme::Surface));
    footer.BorderBrush(theme::brush(theme::Border));
    footer.BorderThickness(Thickness{0, 1, 0, 0});
    footer.Padding(Thickness{16, 9, 16, 9});
    statusText_ = createLabel(L"Inicializando telefone…", 12);
    footer.Child(statusText_);
    Grid::SetRow(footer, 2);
    root_.Children().Append(footer);

    overlay_ = Grid();
    overlay_.Background(theme::brush(theme::Overlay));
    overlay_.Visibility(Visibility::Collapsed);
    settingsPanel_ = buildSettingsPanel().as<Grid>();
    diagnosticsPanel_ = buildDiagnosticsPanel().as<Grid>();
    overlay_.Children().Append(settingsPanel_);
    overlay_.Children().Append(diagnosticsPanel_);
    Grid::SetRowSpan(overlay_, 3);
    root_.Children().Append(overlay_);
    window_.Content(root_);
}

FrameworkElement MainWindow::buildHeader()
{
    Grid header;
    header.Background(theme::brush(theme::Surface));
    header.Padding(Thickness{16, 11, 12, 11});
    header.ColumnDefinitions().Append(ColumnDefinition());
    header.ColumnDefinitions().GetAt(0).Width(star());
    for (int index = 0; index < 3; ++index) {
        header.ColumnDefinitions().Append(ColumnDefinition());
        header.ColumnDefinitions().GetAt(index + 1).Width(automatic());
    }

    StackPanel identity;
    auto title = createLabel(L"POLPhone", 22);
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    title.Foreground(theme::brush(theme::Primary));
    identity.Children().Append(title);
    StackPanel registration;
    registration.Orientation(Orientation::Horizontal);
    registration.Spacing(7);
    registrationDot_ = Border();
    registrationDot_.Width(9); registrationDot_.Height(9);
    registrationDot_.CornerRadius(CornerRadius{5, 5, 5, 5});
    registrationDot_.Background(theme::brush(theme::SubtleText));
    registrationDot_.VerticalAlignment(VerticalAlignment::Center);
    registration.Children().Append(registrationDot_);
    registrationText_ = createLabel(L"Desconectado", 12);
    registration.Children().Append(registrationText_);
    identity.Children().Append(registration);
    header.Children().Append(identity);

    registerButton_ = createButton(L"Conectar", true, false, 78);
    registerButton_.Margin(Thickness{4, 0, 4, 0});
    registerButton_.Click([this](auto&&, auto&&) {
        if (model_.snapshot().registration == core::RegistrationState::Connected) {
            run(controller_->unregisterAccount());
        } else {
            run(controller_->registerAccount());
        }
    });
    Grid::SetColumn(registerButton_, 1);
    header.Children().Append(registerButton_);

    auto settings = createButton(L"⚙", false, false, 42);
    settings.Padding(Thickness{8, 8, 8, 8});
    tooltip(settings, L"Configurações");
    settings.Click([this](auto&&, auto&&) { showOnly(settingsPanel_); });
    Grid::SetColumn(settings, 2);
    header.Children().Append(settings);

    auto diagnostics = createButton(L"ⓘ", false, false, 42);
    diagnostics.Padding(Thickness{8, 8, 8, 8});
    tooltip(diagnostics, L"Diagnóstico");
    diagnostics.Click([this](auto&&, auto&&) { showOnly(diagnosticsPanel_); });
    Grid::SetColumn(diagnostics, 3);
    header.Children().Append(diagnostics);
    return header;
}

FrameworkElement MainWindow::buildDialArea()
{
    Grid host;
    StackPanel panel;
    panel.Spacing(10);
    auto heading = createLabel(L"Digite para ligar", 17);
    heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(heading);

    Grid row;
    row.ColumnSpacing(8);
    row.ColumnDefinitions().Append(ColumnDefinition());
    row.ColumnDefinitions().GetAt(0).Width(star());
    row.ColumnDefinitions().Append(ColumnDefinition());
    row.ColumnDefinitions().GetAt(1).Width(automatic());
    destination_ = TextBox();
    destination_.PlaceholderText(L"Número ou URI SIP");
    destination_.MinHeight(44);
    AutomationProperties::SetName(destination_, L"Número ou URI SIP");
    destination_.KeyDown([this](auto&&, Input::KeyRoutedEventArgs const& args) {
        if (args.Key() == Windows::System::VirtualKey::Enter) {
            startCall();
            args.Handled(true);
        } else if (args.Key() == Windows::System::VirtualKey::Escape) {
            destination_.Text(L"");
            args.Handled(true);
        }
    });
    row.Children().Append(destination_);
    auto clear = createButton(L"⌫", false, false, 44);
    tooltip(clear, L"Apagar número");
    clear.Click([this](auto&&, auto&&) {
        std::wstring value(destination_.Text());
        if (!value.empty()) {
            value.pop_back();
            destination_.Text(value);
        }
    });
    Grid::SetColumn(clear, 1);
    row.Children().Append(clear);
    panel.Children().Append(row);

    callButton_ = createButton(L"Chamar", true, false, 140);
    callButton_.HorizontalAlignment(HorizontalAlignment::Stretch);
    callButton_.Click([this](auto&&, auto&&) { startCall(); });
    panel.Children().Append(callButton_);
    host.Children().Append(card(panel, 15));
    return host;
}

FrameworkElement MainWindow::buildCallArea()
{
    Grid host;
    host.Visibility(Visibility::Collapsed);
    StackPanel panel;
    panel.Spacing(8);
    callStatusText_ = createLabel(L"Sem chamada", 20);
    callStatusText_.HorizontalAlignment(HorizontalAlignment::Center);
    callStatusText_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    callStatusText_.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(callStatusText_);

    Border avatar;
    avatar.Width(66); avatar.Height(66);
    avatar.CornerRadius(CornerRadius{33, 33, 33, 33});
    avatar.Background(theme::brush(theme::PrimarySoft));
    avatar.HorizontalAlignment(HorizontalAlignment::Center);
    auto person = createLabel(L"●", 28);
    person.Foreground(theme::brush(theme::Primary));
    person.HorizontalAlignment(HorizontalAlignment::Center);
    person.VerticalAlignment(VerticalAlignment::Center);
    avatar.Child(person);
    panel.Children().Append(avatar);

    remoteNameText_ = createLabel(L"Desconhecido", 21);
    remoteNameText_.HorizontalAlignment(HorizontalAlignment::Center);
    remoteNameText_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(remoteNameText_);
    remoteText_ = createLabel(L"—", 13);
    remoteText_.Foreground(theme::brush(theme::SubtleText));
    remoteText_.HorizontalAlignment(HorizontalAlignment::Center);
    panel.Children().Append(remoteText_);
    durationText_ = createLabel(L"00:00", 25);
    durationText_.FontFamily(FontFamily(L"Consolas"));
    durationText_.HorizontalAlignment(HorizontalAlignment::Center);
    panel.Children().Append(durationText_);
    audioText_ = createLabel(L"Aguardando áudio", 12);
    audioText_.Foreground(theme::brush(theme::SubtleText));
    audioText_.HorizontalAlignment(HorizontalAlignment::Center);
    panel.Children().Append(audioText_);

    Grid actions;
    actions.ColumnSpacing(8);
    for (int index = 0; index < 3; ++index) actions.ColumnDefinitions().Append(ColumnDefinition());
    answerButton_ = createButton(L"Atender", true, false, 90);
    answerButton_.Click([this](auto&&, auto&&) { run(controller_->answerCall()); });
    actions.Children().Append(answerButton_);
    rejectButton_ = createButton(L"Rejeitar", false, true, 90);
    rejectButton_.Click([this](auto&&, auto&&) { run(controller_->rejectCall()); });
    Grid::SetColumn(rejectButton_, 1);
    actions.Children().Append(rejectButton_);
    muteButton_ = createButton(L"Silenciar", false, false, 90);
    muteButton_.Click([this](auto&&, auto&&) {
        run(controller_->setMuted(!model_.snapshot().muted));
    });
    actions.Children().Append(muteButton_);
    keypadButton_ = createButton(L"Teclado", false, false, 90);
    keypadButton_.Click([this](auto&&, auto&&) {
        keypad_.Visibility(keypad_.Visibility() == Visibility::Visible
            ? Visibility::Collapsed : Visibility::Visible);
    });
    Grid::SetColumn(keypadButton_, 1);
    actions.Children().Append(keypadButton_);
    hangupButton_ = createButton(L"Desligar", false, true, 90);
    hangupButton_.Click([this](auto&&, auto&&) {
        if (model_.beginHangup()) run(controller_->hangupCall());
    });
    Grid::SetColumn(hangupButton_, 2);
    actions.Children().Append(hangupButton_);
    panel.Children().Append(actions);
    host.Children().Append(card(panel, 16));
    return host;
}

FrameworkElement MainWindow::buildKeypad()
{
    StackPanel holder;
    holder.Spacing(7);
    ivrToggle_ = ToggleSwitch();
    ivrToggle_.Header(box_value(L"Modo URA (In-band)"));
    ivrToggle_.Visibility(Visibility::Collapsed);
    ivrToggle_.Toggled([this](auto&&, auto&&) { model_.setIvrMode(ivrToggle_.IsOn()); });
    holder.Children().Append(ivrToggle_);
    keypad_ = Grid();
    keypad_.ColumnSpacing(8);
    keypad_.RowSpacing(8);
    for (int index = 0; index < 3; ++index) keypad_.ColumnDefinitions().Append(ColumnDefinition());
    for (int index = 0; index < 4; ++index) keypad_.RowDefinitions().Append(RowDefinition());
    constexpr wchar_t digits[] = L"123456789*0#";
    constexpr const wchar_t* letters[] = {
        L"", L"ABC", L"DEF", L"GHI", L"JKL", L"MNO",
        L"PQRS", L"TUV", L"WXYZ", L"", L"+", L""};
    for (int index = 0; index < 12; ++index) {
        StackPanel content;
        auto number = createLabel(std::wstring_view(&digits[index], 1), 20);
        number.HorizontalAlignment(HorizontalAlignment::Center);
        content.Children().Append(number);
        auto subtitle = createLabel(letters[index], 9);
        subtitle.Foreground(theme::brush(theme::SubtleText));
        subtitle.HorizontalAlignment(HorizontalAlignment::Center);
        content.Children().Append(subtitle);
        Button button = createButton(L"", false, false, 76);
        button.Content(content);
        button.MinHeight(55);
        const wchar_t digit = digits[index];
        AutomationProperties::SetName(button, hstring(std::wstring_view(&digit, 1)));
        button.Click([this, digit](auto&&, auto&&) { sendDigit(digit); });
        Grid::SetColumn(button, index % 3);
        Grid::SetRow(button, index / 3);
        keypad_.Children().Append(button);
    }
    holder.Children().Append(keypad_);
    return card(holder, 12);
}

FrameworkElement MainWindow::buildSettingsPanel()
{
    Grid host;
    host.Visibility(Visibility::Collapsed);
    host.HorizontalAlignment(HorizontalAlignment::Center);
    host.VerticalAlignment(VerticalAlignment::Center);
    host.Width(420); host.MaxHeight(650);
    ScrollViewer scroll;
    StackPanel panel;
    panel.Background(theme::brush(theme::Surface));
    panel.Padding(Thickness{22, 20, 22, 20}); panel.Spacing(9);
    auto title = createLabel(L"Configurações", 24);
    title.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(title);

    auto accountTitle = createLabel(L"Conta SIP", 17);
    accountTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(accountTitle);
    serverField_ = TextBox(); serverField_.Header(box_value(L"Servidor / proxy")); panel.Children().Append(serverField_);
    registrarField_ = TextBox(); registrarField_.Header(box_value(L"Registrar URI")); panel.Children().Append(registrarField_);
    idUriField_ = TextBox(); idUriField_.Header(box_value(L"Identidade SIP (ID URI)")); panel.Children().Append(idUriField_);
    displayNameField_ = TextBox(); displayNameField_.Header(box_value(L"Nome de exibição")); panel.Children().Append(displayNameField_);
    usernameField_ = TextBox(); usernameField_.Header(box_value(L"Usuário SIP / ramal")); panel.Children().Append(usernameField_);
    authUsernameField_ = TextBox(); authUsernameField_.Header(box_value(L"Usuário de autenticação (Digest)")); panel.Children().Append(authUsernameField_);
    passwordField_ = PasswordBox(); passwordField_.Header(box_value(L"Senha"));
    passwordField_.PlaceholderText(L"Nunca aparece em logs"); panel.Children().Append(passwordField_);
    domainField_ = TextBox(); domainField_.Header(box_value(L"Domínio")); panel.Children().Append(domainField_);
    ComboBox transport; transport.Header(box_value(L"Transporte (motor atual)"));
    transport.Items().Append(box_value(L"UDP")); transport.SelectedIndex(0);
    transport.IsEnabled(false); panel.Children().Append(transport);
    autoRegisterField_ = ToggleSwitch(); autoRegisterField_.Header(box_value(L"Registrar ao iniciar"));
    autoRegisterField_.IsOn(true); panel.Children().Append(autoRegisterField_);

    auto audioTitle = createLabel(L"Áudio", 17);
    audioTitle.Margin(Thickness{0, 9, 0, 0}); audioTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(audioTitle);
    captureField_ = ComboBox(); captureField_.Header(box_value(L"Dispositivo de entrada"));
    captureField_.Items().Append(box_value(L"Padrão do sistema")); captureField_.SelectedIndex(0); panel.Children().Append(captureField_);
    playbackField_ = ComboBox(); playbackField_.Header(box_value(L"Dispositivo de saída"));
    playbackField_.Items().Append(box_value(L"Padrão do sistema")); playbackField_.SelectedIndex(0); panel.Children().Append(playbackField_);
    auto refreshDevices = createButton(L"Atualizar dispositivos", false, false, 150);
    refreshDevices.Click([this](auto&&, auto&&) {
        auto pending = controller_->listAudioDevices();
        std::thread([pending = std::move(pending)]() mutable { static_cast<void>(pending.get()); }).detach();
    });
    panel.Children().Append(refreshDevices);

    auto dtmfTitle = createLabel(L"DTMF", 17);
    dtmfTitle.Margin(Thickness{0, 9, 0, 0}); dtmfTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(dtmfTitle);
    defaultDtmfField_ = ComboBox(); defaultDtmfField_.Header(box_value(L"Método padrão"));
    for (const wchar_t* item : {L"Automático / RFC 4733", L"In-band", L"SIP INFO"}) defaultDtmfField_.Items().Append(box_value(item));
    defaultDtmfField_.SelectedIndex(0); panel.Children().Append(defaultDtmfField_);
    settingsDurationField_ = NumberBox(); settingsDurationField_.Header(box_value(L"Duração (40–2000 ms)"));
    settingsDurationField_.Minimum(40); settingsDurationField_.Maximum(2000); settingsDurationField_.Value(160); panel.Children().Append(settingsDurationField_);
    settingsGapField_ = NumberBox(); settingsGapField_.Header(box_value(L"Intervalo (20–2000 ms)"));
    settingsGapField_.Minimum(20); settingsGapField_.Maximum(2000); settingsGapField_.Value(100); panel.Children().Append(settingsGapField_);
    inbandVolumeField_ = NumberBox(); inbandVolumeField_.Header(box_value(L"Volume In-band (-30–0 dBm0)"));
    inbandVolumeField_.Minimum(-30); inbandVolumeField_.Maximum(0); inbandVolumeField_.Value(-10); panel.Children().Append(inbandVolumeField_);

    auto behaviorTitle = createLabel(L"Aparência e comportamento", 17);
    behaviorTitle.Margin(Thickness{0, 9, 0, 0}); behaviorTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(behaviorTitle);
    ringtoneEnabledField_ = ToggleSwitch();
    ringtoneEnabledField_.Header(box_value(L"Tocar ringtone"));
    ringtoneEnabledField_.IsOn(true); panel.Children().Append(ringtoneEnabledField_);
    topmostIncomingField_ = ToggleSwitch();
    topmostIncomingField_.Header(box_value(L"Manter no topo durante chamada recebida"));
    panel.Children().Append(topmostIncomingField_);
    logLevelField_ = NumberBox(); logLevelField_.Header(box_value(L"Nível de diagnóstico (0–6)"));
    logLevelField_.Minimum(0); logLevelField_.Maximum(6); logLevelField_.Value(4); panel.Children().Append(logLevelField_);

    auto note = createLabel(L"Nenhuma configuração foi alterada.", 12);
    panel.Children().Append(note);
    StackPanel actions; actions.Orientation(Orientation::Horizontal); actions.Spacing(8);
    auto save = createButton(L"Salvar", true);
    save.Click([this, note](auto&&, auto&&) {
        if (demoMode_) { note.Text(L"Modo demo: configuração não gravada."); return; }
        core::GuiSettings settings;
        settings.serverSip = to_string(serverField_.Text());
        settings.registrarUri = to_string(registrarField_.Text());
        settings.idUri = to_string(idUriField_.Text());
        settings.displayName = to_string(displayNameField_.Text());
        settings.username = to_string(usernameField_.Text());
        settings.authUsername = to_string(authUsernameField_.Text());
        settings.password = to_string(passwordField_.Password());
        settings.domain = to_string(domainField_.Text());
        settings.registerOnStartup = autoRegisterField_.IsOn();
        if (captureField_.SelectedIndex() > 0) settings.captureDevice = to_string(unbox_value<hstring>(captureField_.SelectedItem()));
        if (playbackField_.SelectedIndex() > 0) settings.playbackDevice = to_string(unbox_value<hstring>(playbackField_.SelectedItem()));
        settings.dtmfMethod = defaultDtmfField_.SelectedIndex() == 1 ? "inband"
            : (defaultDtmfField_.SelectedIndex() == 2 ? "info" : "rfc4733");
        settings.dtmfDurationMs = static_cast<int>(settingsDurationField_.Value());
        settings.dtmfGapMs = static_cast<int>(settingsGapField_.Value());
        settings.inbandVolumeDbm0 = static_cast<int>(inbandVolumeField_.Value());
        settings.logLevel = static_cast<int>(logLevelField_.Value());
        settings.ringtoneEnabled = ringtoneEnabledField_.IsOn();
        settings.topmostOnIncomingCall = topmostIncomingField_.IsOn();
        const core::DtmfRuntimeSettings runtimeDtmf{
            settings.dtmfMethod == "inband" ? core::DtmfMethod::Inband
                : (settings.dtmfMethod == "info" ? core::DtmfMethod::SipInfo
                                                   : core::DtmfMethod::Rfc4733),
            static_cast<unsigned>(settings.dtmfDurationMs),
            static_cast<unsigned>(settings.dtmfGapMs),
            settings.inbandVolumeDbm0};
        note.Text(L"Validando e salvando…");
        const auto lifetime = lifetime_;
        const auto controller = controller_;
        const auto path = std::filesystem::path(configPath_);
        std::thread([lifetime, this, controller, note, path,
                     runtimeDtmf, settings = std::move(settings)] {
            const auto persisted = core::saveGuiSettings(path, settings);
            if (lifetime->closing.load()) return;
            if (!persisted) {
                lifetime->dispatcher.TryEnqueue([this, lifetime, note, persisted] {
                    if (lifetime->closing.load()) return;
                    note.Text(to_hstring(
                        persisted.error().message + " Campo: " + persisted.error().detail));
                });
                return;
            }
            const auto applied = controller->applyDtmfSettings(runtimeDtmf).get();
            if (lifetime->closing.load()) return;
            lifetime->dispatcher.TryEnqueue([this, lifetime, note, applied] {
                if (lifetime->closing.load()) return;
                if (applied) {
                    passwordField_.Password(L"");
                    model_.setDtmfMethod(core::DtmfMethod::Automatic);
                    ivrToggle_.IsOn(false);
                    note.Text(L"Configuração salva; DTMF aplicado agora. Demais alterações de conta exigem reinício.");
                } else {
                    note.Text(to_hstring(
                        "Configuração salva, mas o DTMF em runtime falhou: "
                        + applied.error().message));
                }
            });
        }).detach();
    });
    actions.Children().Append(save);
    auto reconnect = createButton(L"Testar / reconectar");
    reconnect.Click([this](auto&&, auto&&) { run(controller_->registerAccount()); });
    actions.Children().Append(reconnect);
    auto close = createButton(L"Fechar"); close.Click([this](auto&&, auto&&) { closePanels(); });
    actions.Children().Append(close); panel.Children().Append(actions);
    scroll.Content(panel); host.Children().Append(scroll);
    return host;
}

FrameworkElement MainWindow::buildDiagnosticsPanel()
{
    Grid host;
    host.Visibility(Visibility::Collapsed);
    host.HorizontalAlignment(HorizontalAlignment::Center);
    host.VerticalAlignment(VerticalAlignment::Center);
    host.Width(420); host.MaxHeight(620);
    ScrollViewer scroll;
    StackPanel panel;
    panel.Background(theme::brush(theme::Surface));
    panel.Padding(Thickness{20, 20, 20, 20}); panel.Spacing(10);
    auto title = createLabel(L"Diagnóstico", 24); title.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(title);
    diagnosticsText_ = createLabel(L"Aguardando estado…", 12);
    diagnosticsText_.FontFamily(FontFamily(L"Consolas")); panel.Children().Append(diagnosticsText_);
    StackPanel actions; actions.Orientation(Orientation::Horizontal); actions.Spacing(8);
    auto copy = createButton(L"Copiar diagnóstico", true, false, 130);
    copy.Click([this](auto&&, auto&&) {
        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(diagnosticsText_.Text());
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    });
    actions.Children().Append(copy);
    auto logs = createButton(L"Copiar logs", false, false, 100);
    logs.Click([this](auto&&, auto&&) {
        Windows::ApplicationModel::DataTransfer::DataPackage package;
        std::wstring value;
        for (const auto& line : model_.snapshot().sanitizedLogs) value += to_hstring(line) + L"\n";
        package.SetText(value);
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    });
    actions.Children().Append(logs);
    auto close = createButton(L"Fechar", false, false, 70);
    close.Click([this](auto&&, auto&&) { closePanels(); }); actions.Children().Append(close);
    panel.Children().Append(actions); scroll.Content(panel); host.Children().Append(card(scroll, 0));
    return host;
}

FrameworkElement MainWindow::buildDemoPanel()
{
    StackPanel panel; panel.Spacing(7);
    auto title = createLabel(L"Demonstração", 15); title.Foreground(theme::brush(theme::Primary)); panel.Children().Append(title);
    Grid actions; actions.ColumnSpacing(6);
    for (int i = 0; i < 2; ++i) actions.ColumnDefinitions().Append(ColumnDefinition());
    const std::pair<const wchar_t*, core::DemoScenario> scenarios[] = {
        {L"Receber chamada", core::DemoScenario::IncomingCall},
        {L"Falha de chamada", core::DemoScenario::CallFailure}};
    for (int i = 0; i < 2; ++i) {
        auto button = createButton(scenarios[i].first, false, false, 120);
        const auto scenario = scenarios[i].second;
        button.Click([this, scenario](auto&&, auto&&) { run(controller_->simulateDemo(scenario)); });
        Grid::SetColumn(button, i); actions.Children().Append(button);
    }
    panel.Children().Append(actions); return card(panel, 12);
}

void MainWindow::startCall()
{
    const std::string value = to_string(destination_.Text());
    if (!model_.beginCall(value)) {
        statusText_.Text(to_hstring(model_.validationMessage()));
        statusText_.Foreground(theme::brush(theme::Error));
        return;
    }
    run(controller_->makeCall(value));
}

void MainWindow::sendDigit(wchar_t digit)
{
    if (isIdleLike(model_.snapshot().call)) {
        destination_.Text(destination_.Text() + hstring(std::wstring_view(&digit, 1)));
        destination_.Focus(FocusState::Programmatic);
        return;
    }
    if (!model_.beginDtmf()) return;
    const auto method = model_.ivrMode() ? core::DtmfMethod::Inband : model_.selectedDtmfMethod();
    run(controller_->sendDtmf(std::string(1, static_cast<char>(digit)), method));
}

void MainWindow::showOnly(Grid panel)
{
    overlay_.Visibility(Visibility::Visible);
    settingsPanel_.Visibility(panel == settingsPanel_ ? Visibility::Visible : Visibility::Collapsed);
    diagnosticsPanel_.Visibility(panel == diagnosticsPanel_ ? Visibility::Visible : Visibility::Collapsed);
}

void MainWindow::closePanels()
{
    settingsPanel_.Visibility(Visibility::Collapsed);
    diagnosticsPanel_.Visibility(Visibility::Collapsed);
    overlay_.Visibility(Visibility::Collapsed);
}

void MainWindow::populateSettings(const core::GuiSettings& settings)
{
    serverField_.Text(to_hstring(settings.serverSip));
    registrarField_.Text(to_hstring(settings.registrarUri));
    idUriField_.Text(to_hstring(settings.idUri));
    displayNameField_.Text(to_hstring(settings.displayName));
    usernameField_.Text(to_hstring(settings.username));
    authUsernameField_.Text(to_hstring(settings.authUsername));
    domainField_.Text(to_hstring(settings.domain));
    autoRegisterField_.IsOn(settings.registerOnStartup);
    defaultDtmfField_.SelectedIndex(settings.dtmfMethod == "inband" ? 1
        : (settings.dtmfMethod == "info" ? 2 : 0));
    settingsDurationField_.Value(settings.dtmfDurationMs);
    settingsGapField_.Value(settings.dtmfGapMs);
    inbandVolumeField_.Value(settings.inbandVolumeDbm0);
    logLevelField_.Value(settings.logLevel);
    ringtoneEnabledField_.IsOn(settings.ringtoneEnabled);
    topmostIncomingField_.IsOn(settings.topmostOnIncomingCall);
    if (!settings.captureDevice.empty()) {
        captureField_.Items().Append(box_value(to_hstring(settings.captureDevice)));
        captureField_.SelectedIndex(1);
    }
    if (!settings.playbackDevice.empty()) {
        playbackField_.Items().Append(box_value(to_hstring(settings.playbackDevice)));
        playbackField_.SelectedIndex(1);
    }
}

void MainWindow::run(std::future<util::Result<void>> operation)
{
    const auto lifetime = lifetime_;
    std::thread([lifetime, this, operation = std::move(operation)]() mutable {
        auto result = operation.get();
        if (!result && !lifetime->closing.load()) {
            const util::Error error = result.error();
            lifetime->dispatcher.TryEnqueue([this, lifetime, error] {
                if (!lifetime->closing.load()) showOperationError(error);
            });
        }
    }).detach();
}

void MainWindow::showOperationError(const util::Error& error)
{
    model_.completeCallCommand(); model_.completeHangupCommand(); model_.completeDtmf();
    statusText_.Text(to_hstring(error.message));
    statusText_.Foreground(theme::brush(theme::Error));
}

void MainWindow::updateIncomingPresentation(const core::TelephonySnapshot& snapshot)
{
    const bool incoming = snapshot.call == core::CallState::IncomingRinging;
    if (incoming && snapshot.ringtoneEnabled) ringtone_.start();
    else ringtone_.stop();

    const bool makeTopmost = incoming && snapshot.topmostOnIncomingCall;
    if (makeTopmost == topmostApplied_) return;
    HWND hwnd{};
    if (SUCCEEDED(window_.as<::IWindowNative>()->get_WindowHandle(&hwnd)) && hwnd != nullptr) {
        SetWindowPos(hwnd, makeTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        topmostApplied_ = makeTopmost;
    }
}

void MainWindow::render(const core::TelephonySnapshot& snapshot)
{
    model_.update(snapshot);
    model_.completeCallCommand();
    if (snapshot.call == core::CallState::Idle || snapshot.call == core::CallState::Disconnected
        || snapshot.call == core::CallState::Error) model_.completeHangupCommand();
    if (!snapshot.dtmfInFlight) model_.completeDtmf();
    const auto commands = model_.commands();
    updateIncomingPresentation(snapshot);

    const auto appendDevice = [](const ComboBox& field, const std::string& name) {
        const auto text = to_hstring(name);
        for (const auto& item : field.Items()) {
            if (unbox_value<hstring>(item) == text) return;
        }
        field.Items().Append(box_value(text));
    };
    if (captureField_) for (const auto& device : snapshot.devices) {
        if (device.capture) appendDevice(captureField_, device.name);
    }
    if (playbackField_) for (const auto& device : snapshot.devices) {
        if (device.playback) appendDevice(playbackField_, device.name);
    }

    registrationText_.Text(to_hstring(core::registrationStateText(snapshot.registration)));
    registrationDot_.Background(theme::brush(
        snapshot.registration == core::RegistrationState::Connected ? theme::Success
        : (snapshot.registration == core::RegistrationState::Failed ? theme::Error : theme::SubtleText)));
    registerButton_.IsEnabled(commands.registerAccount || commands.unregisterAccount);
    registerButton_.Content(box_value(commands.unregisterAccount ? L"Desconectar" : L"Conectar"));

    const bool idle = isIdleLike(snapshot.call);
    dialPanel_.Visibility(idle ? Visibility::Visible : Visibility::Collapsed);
    callPanel_.Visibility(idle ? Visibility::Collapsed : Visibility::Visible);
    callButton_.IsEnabled(commands.makeCall && !destination_.Text().empty());
    destination_.IsEnabled(commands.makeCall);
    callStatusText_.Text(to_hstring(core::callStateText(snapshot.call)));
    const auto fallback = snapshot.remoteNumber.empty() ? snapshot.maskedRemote : snapshot.remoteNumber;
    remoteNameText_.Text(to_hstring(snapshot.remoteDisplayName.empty() ? fallback : snapshot.remoteDisplayName));
    remoteText_.Text(to_hstring(fallback.empty() ? std::string("—") : fallback));
    durationText_.Text(to_hstring(core::formatCallDuration(snapshot.callDurationSeconds)));
    durationText_.Visibility(snapshot.call == core::CallState::Active ? Visibility::Visible : Visibility::Collapsed);
    audioText_.Text(snapshot.call == core::CallState::Active
        ? to_hstring(snapshot.codec.empty() ? std::string(core::mediaStateText(snapshot.media)) : snapshot.codec)
        : L"Aguardando conexão");

    const bool incoming = snapshot.call == core::CallState::IncomingRinging;
    const bool active = snapshot.call == core::CallState::Active;
    answerButton_.Visibility(incoming ? Visibility::Visible : Visibility::Collapsed);
    rejectButton_.Visibility(incoming ? Visibility::Visible : Visibility::Collapsed);
    answerButton_.IsEnabled(commands.answerCall); rejectButton_.IsEnabled(commands.rejectCall);
    muteButton_.Visibility(active ? Visibility::Visible : Visibility::Collapsed);
    keypadButton_.Visibility(active ? Visibility::Visible : Visibility::Collapsed);
    muteButton_.IsEnabled(commands.mute); keypadButton_.IsEnabled(commands.keypad);
    muteButton_.Content(box_value(snapshot.muted ? L"Reativar microfone" : L"Silenciar"));
    tooltip(muteButton_, snapshot.muted ? L"Reativar microfone" : L"Silenciar microfone");
    hangupButton_.Visibility(!incoming ? Visibility::Visible : Visibility::Collapsed);
    hangupButton_.IsEnabled(commands.hangupCall);
    hangupButton_.Content(box_value(
        snapshot.call == core::CallState::OutgoingDialing
            || snapshot.call == core::CallState::OutgoingRinging ? L"Cancelar" : L"Desligar"));

    if (idle) keypad_.Visibility(Visibility::Visible);
    else if (!active) keypad_.Visibility(Visibility::Collapsed);
    keypad_.IsHitTestVisible(idle || commands.sendDtmf);
    keypad_.Opacity(idle || commands.sendDtmf ? 1.0 : 0.55);
    ivrToggle_.Visibility(active && keypad_.Visibility() == Visibility::Visible
        ? Visibility::Visible : Visibility::Collapsed);

    if (!snapshot.friendlyError.empty()) {
        statusText_.Text(to_hstring(snapshot.friendlyError));
        statusText_.Foreground(theme::brush(theme::Error));
    } else {
        statusText_.Foreground(theme::brush(theme::SubtleText));
        if (snapshot.call == core::CallState::IncomingRinging) statusText_.Text(L"Chamada recebida — escolha Atender ou Rejeitar.");
        else if (snapshot.call == core::CallState::Disconnected) statusText_.Text(L"Chamada encerrada.");
        else if (snapshot.registration == core::RegistrationState::Connected) statusText_.Text(L"Conectado ao servidor SIP.");
        else statusText_.Text(to_hstring(core::registrationStateText(snapshot.registration)));
    }

    std::wostringstream diagnostic;
    diagnostic << L"POLPhone 0.1.0\n"
               << L"Backend: " << (snapshot.demoMode ? L"Demonstração" : L"SIP real / PJSIP 2.17") << L'\n'
               << L"User-Agent: " << (snapshot.userAgent.empty() ? L"—" : to_hstring(snapshot.userAgent).c_str()) << L'\n'
               << L"Servidor: " << (snapshot.registrarUri.empty() ? L"—" : to_hstring(snapshot.registrarUri).c_str()) << L'\n'
               << L"Transporte: " << (snapshot.transport.empty() ? L"—" : to_hstring(snapshot.transport).c_str()) << L'\n'
               << L"Ramal: " << (snapshot.sipUsername.empty() ? L"—" : to_hstring(snapshot.sipUsername).c_str()) << L'\n'
               << L"Último código SIP: " << snapshot.latestSipCode << L'\n'
               << L"Endpoint: " << to_hstring(snapshot.endpointState).c_str() << L'\n'
               << L"Registro: " << to_hstring(core::registrationStateText(snapshot.registration)).c_str() << L'\n'
               << L"Estado da chamada: " << to_hstring(core::callStateText(snapshot.call)).c_str() << L'\n'
               << L"Direção: " << to_hstring(core::callDirectionText(snapshot.callDirection)).c_str() << L'\n'
               << L"URI remota: " << (snapshot.maskedRemote.empty() ? L"—" : to_hstring(snapshot.maskedRemote).c_str()) << L'\n'
               << L"Codec: " << (snapshot.codec.empty() ? L"—" : to_hstring(snapshot.codec).c_str()) << L'\n'
               << L"RTP local: " << (snapshot.localRtp.empty() ? L"—" : to_hstring(snapshot.localRtp).c_str()) << L'\n'
               << L"RTP remoto: " << (snapshot.remoteRtp.empty() ? L"—" : to_hstring(snapshot.remoteRtp).c_str()) << L'\n'
               << L"Pacotes enviados/recebidos: " << snapshot.packetsSent << L"/" << snapshot.packetsReceived << L'\n'
               << L"Perda: " << (snapshot.hasRtpStatistics ? std::to_wstring(snapshot.packetsLost) : L"—") << L'\n'
               << L"Jitter: " << (snapshot.hasRtpStatistics ? std::to_wstring(snapshot.jitterMs) + L" ms" : L"—") << L'\n'
               << L"Mídia: " << to_hstring(core::mediaStateText(snapshot.media)).c_str() << L'\n'
               << L"Entrada: " << (snapshot.captureDevice ? std::to_wstring(*snapshot.captureDevice) : L"padrão") << L'\n'
               << L"Saída: " << (snapshot.playbackDevice ? std::to_wstring(*snapshot.playbackDevice) : L"padrão") << L'\n'
               << L"DTMF configurado: " << to_hstring(core::dtmfMethodText(snapshot.dtmfConfiguredMethod)).c_str() << L'\n'
               << L"DTMF efetivo: " << to_hstring(core::dtmfMethodText(snapshot.dtmfEffectiveMethod)).c_str()
               << L" (" << snapshot.dtmfDurationMs << L"/" << snapshot.dtmfGapMs
               << L" ms, " << snapshot.inbandVolumeDbm0 << L" dBm0)\n"
               << L"Último método DTMF: "
               << (snapshot.lastDtmfMethod.has_value()
                       ? to_hstring(core::dtmfMethodText(*snapshot.lastDtmfMethod)).c_str()
                       : L"—") << L'\n'
               << L"Resultado do último DTMF: " << to_hstring(snapshot.lastDtmfResult).c_str() << L'\n'
               << L"Detalhe: " << (snapshot.technicalDetail.empty() ? L"—" : to_hstring(snapshot.technicalDetail).c_str()) << L"\n\n"
               << L"Logs sanitizados disponíveis no botão Copiar logs.";
    diagnosticsText_.Text(diagnostic.str());
}

} // namespace polphone::gui
