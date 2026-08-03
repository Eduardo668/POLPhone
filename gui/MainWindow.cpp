/* POLPhone - janela principal WinUI 3. GPL-2.0-only. */

#include "MainWindow.h"

#include "Theme.h"
#include "core/ControllerFactory.h"
#include "core/SettingsService.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>

#include <filesystem>
#include <sstream>
#include <thread>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace polphone::gui {
namespace {

GridLength automatic() { return GridLength{0, GridUnitType::Auto}; }
GridLength star() { return GridLength{1, GridUnitType::Star}; }

Border card(UIElement const& content)
{
    Border border;
    border.Background(theme::brush(theme::Surface));
    border.BorderBrush(theme::brush(theme::Border));
    border.BorderThickness(Thickness{1, 1, 1, 1});
    border.CornerRadius(CornerRadius{10, 10, 10, 10});
    border.Padding(Thickness{20, 20, 20, 20});
    border.Child(content);
    return border;
}

core::DtmfMethod methodFromIndex(int index)
{
    switch (index) {
    case 1: return core::DtmfMethod::Rfc4733;
    case 2: return core::DtmfMethod::Inband;
    case 3: return core::DtmfMethod::SipInfo;
    default: return core::DtmfMethod::Automatic;
    }
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
    controller_->setStateChangedHandler(
        [lifetime = lifetime_, this](const core::TelephonySnapshot& snapshot) {
            if (!lifetime->closing.load()) queueRender(snapshot);
        });
    window_.Closed([this](auto&&, auto&&) {
        lifetime_->closing = true;
        controller_->setStateChangedHandler({});
        static_cast<void>(controller_->shutdown());
    });
    render(controller_->getState());
    run(controller_->initialize());
}

MainWindow::~MainWindow()
{
    lifetime_->closing = true;
    if (controller_) controller_->setStateChangedHandler({});
}

void MainWindow::activate() { window_.Activate(); }

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
    std::wstring_view text,
    bool primary,
    bool destructive)
{
    Button button;
    button.Content(box_value(hstring(text)));
    button.MinWidth(108);
    button.MinHeight(44);
    button.Padding(Thickness{18, 10, 18, 10});
    button.CornerRadius(CornerRadius{7, 7, 7, 7});
    const auto applyNormal = [button, primary, destructive] {
        const auto fill = destructive ? theme::Error
            : (primary ? theme::Primary : theme::Surface);
        button.Background(theme::brush(fill));
        button.Foreground(theme::brush(
            primary || destructive ? theme::Surface : theme::Primary));
        button.BorderBrush(theme::brush(
            destructive ? theme::Error : theme::Primary));
        button.Opacity(1.0);
    };
    const auto applyHover = [button, primary, destructive] {
        const auto fill = destructive ? theme::ErrorHover
            : (primary ? theme::PrimaryHover : theme::PrimarySoft);
        button.Background(theme::brush(fill));
        button.Foreground(theme::brush(
            primary || destructive ? theme::Surface : theme::Primary));
    };
    const auto applyPressed = [button, primary, destructive] {
        const auto fill = destructive ? theme::ErrorPressed
            : (primary ? theme::PrimaryPressed : theme::Border);
        button.Background(theme::brush(fill));
        button.Foreground(theme::brush(
            primary || destructive ? theme::Surface : theme::Primary));
    };
    button.BorderThickness(Thickness{1, 1, 1, 1});
    applyNormal();
    button.PointerEntered([button, applyHover](auto&&, auto&&) {
        if (button.IsEnabled()) applyHover();
    });
    button.PointerExited([button, applyNormal](auto&&, auto&&) {
        if (button.IsEnabled()) applyNormal();
    });
    button.PointerPressed([button, applyPressed](auto&&, auto&&) {
        if (button.IsEnabled()) applyPressed();
    });
    button.PointerReleased([button, applyHover](auto&&, auto&&) {
        if (button.IsEnabled()) applyHover();
    });
    button.IsEnabledChanged([button, applyNormal](auto&&, auto&&) {
        if (button.IsEnabled()) {
            applyNormal();
        } else {
            button.Background(theme::brush(theme::PrimarySoft));
            button.Foreground(theme::brush(theme::SubtleText));
            button.BorderBrush(theme::brush(theme::Border));
            button.Opacity(0.72);
        }
    });
    return button;
}

void MainWindow::buildLayout()
{
    root_ = Grid();
    root_.Background(theme::brush(theme::SurfaceMuted));
    root_.RowDefinitions().Append(RowDefinition());
    root_.RowDefinitions().GetAt(0).Height(automatic());
    root_.RowDefinitions().Append(RowDefinition());
    root_.RowDefinitions().GetAt(1).Height(star());

    auto header = buildHeader();
    Grid::SetRow(header, 0);
    root_.Children().Append(header);

    ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    StackPanel body;
    body.Spacing(16);
    body.Padding(Thickness{28, 24, 28, 32});
    body.MaxWidth(920);
    body.HorizontalAlignment(HorizontalAlignment::Center);
    body.Children().Append(buildDialArea());
    body.Children().Append(buildCallCard());
    body.Children().Append(buildKeypad());
    if (demoMode_) body.Children().Append(buildDemoPanel());
    scroll.Content(body);
    Grid::SetRow(scroll, 1);
    root_.Children().Append(scroll);

    overlay_ = Grid();
    overlay_.Background(theme::brush(theme::Overlay));
    overlay_.Visibility(Visibility::Collapsed);
    settingsPanel_ = buildSettingsPanel().as<Grid>();
    diagnosticsPanel_ = buildDiagnosticsPanel().as<Grid>();
    overlay_.Children().Append(settingsPanel_);
    overlay_.Children().Append(diagnosticsPanel_);
    Grid::SetRowSpan(overlay_, 2);
    root_.Children().Append(overlay_);

    window_.Content(root_);
}

FrameworkElement MainWindow::buildHeader()
{
    Grid header;
    header.Background(theme::brush(theme::Surface));
    header.Padding(Thickness{28, 16, 28, 16});
    header.ColumnDefinitions().Append(ColumnDefinition());
    header.ColumnDefinitions().GetAt(0).Width(star());
    header.ColumnDefinitions().Append(ColumnDefinition());
    header.ColumnDefinitions().GetAt(1).Width(automatic());
    header.ColumnDefinitions().Append(ColumnDefinition());
    header.ColumnDefinitions().GetAt(2).Width(automatic());
    header.ColumnDefinitions().Append(ColumnDefinition());
    header.ColumnDefinitions().GetAt(3).Width(automatic());

    StackPanel identity;
    identity.Orientation(Orientation::Horizontal);
    identity.Spacing(16);
    auto title = createLabel(L"POLPhone", 28);
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    title.Foreground(theme::brush(theme::Primary));
    identity.Children().Append(title);
    registrationText_ = createLabel(L"Desconectado", 14);
    registrationText_.VerticalAlignment(VerticalAlignment::Center);
    identity.Children().Append(registrationText_);
    header.Children().Append(identity);

    registerButton_ = createButton(L"Registrar", true);
    registerButton_.Margin(Thickness{8, 0, 8, 0});
    registerButton_.Click([this](auto&&, auto&&) {
        if (model_.snapshot().registration == core::RegistrationState::Connected) {
            run(controller_->unregisterAccount());
        } else {
            run(controller_->registerAccount());
        }
    });
    Grid::SetColumn(registerButton_, 1);
    header.Children().Append(registerButton_);

    auto settings = createButton(L"Configurações");
    settings.Margin(Thickness{8, 0, 8, 0});
    settings.Click([this](auto&&, auto&&) { showOnly(settingsPanel_); });
    Grid::SetColumn(settings, 2);
    header.Children().Append(settings);

    auto diagnostics = createButton(L"Diagnóstico");
    diagnostics.Click([this](auto&&, auto&&) { showOnly(diagnosticsPanel_); });
    Grid::SetColumn(diagnostics, 3);
    header.Children().Append(diagnostics);
    return header;
}

FrameworkElement MainWindow::buildDialArea()
{
    StackPanel panel;
    panel.Spacing(12);
    auto heading = createLabel(L"Nova ligação", 20);
    heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(heading);

    Grid row;
    row.ColumnSpacing(10);
    row.ColumnDefinitions().Append(ColumnDefinition());
    row.ColumnDefinitions().GetAt(0).Width(star());
    row.ColumnDefinitions().Append(ColumnDefinition());
    row.ColumnDefinitions().GetAt(1).Width(automatic());
    row.ColumnDefinitions().Append(ColumnDefinition());
    row.ColumnDefinitions().GetAt(2).Width(automatic());
    destination_ = TextBox();
    destination_.PlaceholderText(L"Número ou URI SIP");
    destination_.MinHeight(48);
    row.Children().Append(destination_);
    auto clear = createButton(L"Apagar");
    clear.Click([this](auto&&, auto&&) { destination_.Text(L""); });
    Grid::SetColumn(clear, 1);
    row.Children().Append(clear);
    callButton_ = createButton(L"Chamar", true);
    callButton_.Click([this](auto&&, auto&&) {
        const std::string destination = to_string(destination_.Text());
        if (!model_.beginCall(destination)) {
            errorText_.Text(to_hstring(model_.validationMessage()));
            return;
        }
        run(controller_->makeCall(destination));
    });
    Grid::SetColumn(callButton_, 2);
    row.Children().Append(callButton_);
    panel.Children().Append(row);
    return card(panel);
}

FrameworkElement MainWindow::buildCallCard()
{
    StackPanel panel;
    panel.Spacing(12);
    callStatusText_ = createLabel(L"Sem chamada", 24);
    callStatusText_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    callStatusText_.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(callStatusText_);
    remoteText_ = createLabel(L"Destino: —", 16);
    panel.Children().Append(remoteText_);
    durationText_ = createLabel(L"00:00", 30);
    durationText_.FontFamily(FontFamily(L"Consolas"));
    panel.Children().Append(durationText_);
    audioText_ = createLabel(L"Áudio inativo", 14);
    panel.Children().Append(audioText_);

    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(10);
    answerButton_ = createButton(L"Atender", true);
    answerButton_.Click([this](auto&&, auto&&) { run(controller_->answerCall()); });
    actions.Children().Append(answerButton_);
    rejectButton_ = createButton(L"Rejeitar");
    rejectButton_.Click([this](auto&&, auto&&) { run(controller_->rejectCall()); });
    actions.Children().Append(rejectButton_);
    hangupButton_ = createButton(L"Desligar", false, true);
    hangupButton_.Click([this](auto&&, auto&&) {
        if (model_.beginHangup()) run(controller_->hangupCall());
    });
    actions.Children().Append(hangupButton_);
    muteButton_ = createButton(L"Ativar mudo");
    muteButton_.Click([this](auto&&, auto&&) {
        run(controller_->setMuted(!model_.snapshot().muted));
    });
    actions.Children().Append(muteButton_);
    keypadButton_ = createButton(L"Teclado");
    keypadButton_.Click([this](auto&&, auto&&) {
        keypad_.Visibility(keypad_.Visibility() == Visibility::Visible
            ? Visibility::Collapsed : Visibility::Visible);
    });
    actions.Children().Append(keypadButton_);
    panel.Children().Append(actions);

    Grid dtmf;
    dtmf.ColumnSpacing(12);
    for (int index = 0; index < 3; ++index) dtmf.ColumnDefinitions().Append(ColumnDefinition());
    dtmfMethod_ = ComboBox();
    for (const wchar_t* item : {L"Automático", L"RFC 4733", L"In-band", L"SIP INFO"}) {
        dtmfMethod_.Items().Append(box_value(item));
    }
    dtmfMethod_.SelectedIndex(0);
    dtmfMethod_.Header(box_value(L"Método DTMF"));
    dtmfMethod_.SelectionChanged([this](auto&&, auto&&) {
        model_.setDtmfMethod(methodFromIndex(dtmfMethod_.SelectedIndex()));
        if (!model_.ivrMode()) {
            ivrToggle_.IsOn(false);
            ivrText_.Text(L"Modo URA inativo");
        }
    });
    dtmf.Children().Append(dtmfMethod_);
    durationBox_ = NumberBox();
    durationBox_.Header(box_value(L"Duração (ms)"));
    durationBox_.Minimum(40); durationBox_.Maximum(2000); durationBox_.Value(160);
    Grid::SetColumn(durationBox_, 1); dtmf.Children().Append(durationBox_);
    gapBox_ = NumberBox();
    gapBox_.Header(box_value(L"Intervalo (ms)"));
    gapBox_.Minimum(20); gapBox_.Maximum(2000); gapBox_.Value(100);
    Grid::SetColumn(gapBox_, 2); dtmf.Children().Append(gapBox_);
    panel.Children().Append(dtmf);

    ivrToggle_ = ToggleSwitch();
    ivrToggle_.Header(box_value(L"Modo URA"));
    ivrToggle_.OnContent(box_value(L"Ativo — envio somente In-band"));
    ivrToggle_.OffContent(box_value(L"Inativo"));
    ivrToggle_.Toggled([this](auto&&, auto&&) {
        model_.setIvrMode(ivrToggle_.IsOn());
        ivrText_.Text(to_hstring(model_.ivrStatusText()));
        if (model_.ivrMode()) dtmfMethod_.SelectedIndex(2);
    });
    panel.Children().Append(ivrToggle_);
    ivrText_ = createLabel(L"Modo URA inativo", 14);
    ivrText_.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(ivrText_);
    errorText_ = createLabel(L"", 14);
    errorText_.Foreground(theme::brush(theme::Error));
    panel.Children().Append(errorText_);
    return card(panel);
}

FrameworkElement MainWindow::buildKeypad()
{
    keypad_ = Grid();
    keypad_.Visibility(Visibility::Collapsed);
    keypad_.ColumnSpacing(10);
    keypad_.RowSpacing(10);
    for (int index = 0; index < 3; ++index) keypad_.ColumnDefinitions().Append(ColumnDefinition());
    for (int index = 0; index < 4; ++index) keypad_.RowDefinitions().Append(RowDefinition());
    constexpr wchar_t digits[] = L"123456789*0#";
    for (int index = 0; index < 12; ++index) {
        Button button = createButton(std::wstring_view(&digits[index], 1), true);
        button.MinHeight(56);
        const wchar_t digit = digits[index];
        button.Click([this, digit](auto&&, auto&&) { sendDigit(digit); });
        Grid::SetColumn(button, index % 3);
        Grid::SetRow(button, index / 3);
        keypad_.Children().Append(button);
    }
    return card(keypad_);
}

FrameworkElement MainWindow::buildSettingsPanel()
{
    Grid host;
    host.Visibility(Visibility::Collapsed);
    host.HorizontalAlignment(HorizontalAlignment::Center);
    host.VerticalAlignment(VerticalAlignment::Center);
    host.MaxWidth(760);
    host.MaxHeight(760);
    ScrollViewer scroll;
    StackPanel panel;
    panel.Background(theme::brush(theme::Surface));
    panel.Padding(Thickness{28, 28, 28, 28});
    panel.Spacing(10);
    auto title = createLabel(L"Configurações", 26);
    title.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(title);
    panel.Children().Append(createLabel(
        demoMode_
            ? L"Modo demo: alterações são temporárias e nenhuma credencial será gravada."
            : L"Os valores são validados pelo mesmo modelo do núcleo; reinicie o motor após confirmar.",
        13));
    serverField_ = TextBox(); serverField_.Header(box_value(L"Servidor SIP / proxy URI"));
    serverField_.PlaceholderText(L"Opcional; exemplo: sip:proxy.empresa.local");
    panel.Children().Append(serverField_);
    registrarField_ = TextBox(); registrarField_.Header(box_value(L"Registrar URI"));
    registrarField_.PlaceholderText(L"sip:servidor.empresa.local");
    panel.Children().Append(registrarField_);
    idUriField_ = TextBox(); idUriField_.Header(box_value(L"ID URI"));
    idUriField_.PlaceholderText(L"sip:usuario@dominio"); panel.Children().Append(idUriField_);
    usernameField_ = TextBox(); usernameField_.Header(box_value(L"Usuário"));
    panel.Children().Append(usernameField_);
    passwordField_ = PasswordBox(); passwordField_.Header(box_value(L"Senha"));
    passwordField_.PlaceholderText(L"Nunca exibida em logs");
    panel.Children().Append(passwordField_);
    domainField_ = TextBox(); domainField_.Header(box_value(L"Domínio"));
    panel.Children().Append(domainField_);
    ComboBox transport;
    transport.Header(box_value(L"Transporte"));
    transport.Items().Append(box_value(L"UDP"));
    transport.SelectedIndex(0);
    panel.Children().Append(transport);
    autoRegisterField_ = ToggleSwitch();
    autoRegisterField_.Header(box_value(L"Registro automático")); autoRegisterField_.IsOn(true);
    panel.Children().Append(autoRegisterField_);
    captureField_ = ComboBox(); captureField_.Header(box_value(L"Microfone"));
    captureField_.Items().Append(box_value(L"Padrão do sistema")); captureField_.SelectedIndex(0);
    panel.Children().Append(captureField_);
    playbackField_ = ComboBox(); playbackField_.Header(box_value(L"Saída de áudio"));
    playbackField_.Items().Append(box_value(L"Padrão do sistema")); playbackField_.SelectedIndex(0);
    panel.Children().Append(playbackField_);
    defaultDtmfField_ = ComboBox(); defaultDtmfField_.Header(box_value(L"Método DTMF padrão"));
    for (const wchar_t* item : {L"RFC 4733", L"In-band", L"SIP INFO"}) {
        defaultDtmfField_.Items().Append(box_value(item));
    }
    defaultDtmfField_.SelectedIndex(0); panel.Children().Append(defaultDtmfField_);
    settingsDurationField_ = NumberBox(); settingsDurationField_.Header(box_value(L"Duração DTMF (40–2000 ms)"));
    settingsDurationField_.Minimum(40); settingsDurationField_.Maximum(2000); settingsDurationField_.Value(160);
    panel.Children().Append(settingsDurationField_);
    settingsGapField_ = NumberBox(); settingsGapField_.Header(box_value(L"Intervalo DTMF (20–2000 ms)"));
    settingsGapField_.Minimum(20); settingsGapField_.Maximum(2000); settingsGapField_.Value(100);
    panel.Children().Append(settingsGapField_);
    inbandVolumeField_ = NumberBox(); inbandVolumeField_.Header(box_value(L"Volume In-band (-30–0 dBm0)"));
    inbandVolumeField_.Minimum(-30); inbandVolumeField_.Maximum(0); inbandVolumeField_.Value(-10);
    panel.Children().Append(inbandVolumeField_);
    logLevelField_ = NumberBox(); logLevelField_.Header(box_value(L"Nível de log (0–6)"));
    logLevelField_.Minimum(0); logLevelField_.Maximum(6); logLevelField_.Value(4);
    panel.Children().Append(logLevelField_);
    auto note = createLabel(L"Nenhuma configuração foi alterada.", 13);
    panel.Children().Append(note);
    StackPanel actions;
    actions.Orientation(Orientation::Horizontal); actions.Spacing(10);
    auto confirm = createButton(L"Confirmar alterações", true);
    confirm.Click([this, note](auto&&, auto&&) {
        if (demoMode_) {
            note.Text(L"Configuração temporária confirmada; nenhum arquivo ou credencial foi gravado.");
            return;
        }
        core::GuiSettings settings;
        settings.serverSip = to_string(serverField_.Text());
        settings.registrarUri = to_string(registrarField_.Text());
        settings.idUri = to_string(idUriField_.Text());
        settings.username = to_string(usernameField_.Text());
        settings.password = to_string(passwordField_.Password());
        settings.domain = to_string(domainField_.Text());
        settings.registerOnStartup = autoRegisterField_.IsOn();
        if (captureField_.SelectedIndex() > 0) {
            settings.captureDevice = to_string(unbox_value<hstring>(captureField_.SelectedItem()));
        }
        if (playbackField_.SelectedIndex() > 0) {
            settings.playbackDevice = to_string(unbox_value<hstring>(playbackField_.SelectedItem()));
        }
        settings.dtmfMethod = defaultDtmfField_.SelectedIndex() == 1
            ? "inband" : (defaultDtmfField_.SelectedIndex() == 2 ? "info" : "rfc4733");
        settings.dtmfDurationMs = static_cast<int>(settingsDurationField_.Value());
        settings.dtmfGapMs = static_cast<int>(settingsGapField_.Value());
        settings.inbandVolumeDbm0 = static_cast<int>(inbandVolumeField_.Value());
        settings.logLevel = static_cast<int>(logLevelField_.Value());
        note.Text(L"Validando e salvando...");
        const auto lifetime = lifetime_;
        const auto path = std::filesystem::path(configPath_);
        std::thread([this, lifetime, note, path, settings = std::move(settings)] {
            const auto result = core::saveGuiSettings(path, settings);
            if (lifetime->closing.load()) return;
            lifetime->dispatcher.TryEnqueue([this, lifetime, note, result] {
                if (lifetime->closing.load()) return;
                if (result) {
                    passwordField_.Password(L"");
                    note.Text(L"Configuração salva. Reinicie o POLPhone para reinicializar o motor SIP.");
                } else {
                    note.Text(to_hstring(result.error().message + " Campo: " + result.error().detail));
                }
            });
        }).detach();
    });
    actions.Children().Append(confirm);
    auto close = createButton(L"Fechar");
    close.Click([this](auto&&, auto&&) { closePanels(); });
    actions.Children().Append(close);
    panel.Children().Append(actions);
    scroll.Content(panel);
    host.Children().Append(scroll);
    return host;
}

FrameworkElement MainWindow::buildDiagnosticsPanel()
{
    Grid host;
    host.Visibility(Visibility::Collapsed);
    host.HorizontalAlignment(HorizontalAlignment::Center);
    host.VerticalAlignment(VerticalAlignment::Center);
    host.MaxWidth(820); host.MaxHeight(700);
    StackPanel panel;
    panel.Background(theme::brush(theme::Surface));
    panel.Padding(Thickness{28, 28, 28, 28}); panel.Spacing(12);
    auto title = createLabel(L"Diagnóstico sanitizado", 26);
    title.Foreground(theme::brush(theme::Primary));
    panel.Children().Append(title);
    diagnosticsText_ = createLabel(L"Aguardando estado...", 13);
    diagnosticsText_.FontFamily(FontFamily(L"Consolas"));
    panel.Children().Append(diagnosticsText_);
    StackPanel actions; actions.Orientation(Orientation::Horizontal); actions.Spacing(10);
    auto copy = createButton(L"Copiar informações", true);
    copy.Click([this](auto&&, auto&&) {
        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(diagnosticsText_.Text());
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    });
    actions.Children().Append(copy);
    auto close = createButton(L"Fechar");
    close.Click([this](auto&&, auto&&) { closePanels(); });
    actions.Children().Append(close);
    panel.Children().Append(actions);
    host.Children().Append(card(panel));
    return host;
}

FrameworkElement MainWindow::buildDemoPanel()
{
    StackPanel panel;
    panel.Spacing(10);
    auto title = createLabel(L"Cenários de demonstração", 18);
    title.Foreground(theme::brush(theme::Primary)); panel.Children().Append(title);
    auto description = createLabel(
        L"Estes controles não acessam PABX, rede nem configuração SIP real.", 13);
    panel.Children().Append(description);
    StackPanel actions; actions.Orientation(Orientation::Horizontal); actions.Spacing(8);
    const std::pair<const wchar_t*, core::DemoScenario> scenarios[] = {
        {L"Chamada recebida", core::DemoScenario::IncomingCall},
        {L"Falha de registro", core::DemoScenario::RegistrationFailure},
        {L"Falha de chamada", core::DemoScenario::CallFailure},
        {L"Perder conexão", core::DemoScenario::ConnectionLoss},
    };
    for (const auto& [text, scenario] : scenarios) {
        auto button = createButton(text);
        button.Click([this, scenario](auto&&, auto&&) {
            run(controller_->simulateDemo(scenario));
        });
        actions.Children().Append(button);
    }
    panel.Children().Append(actions);
    return card(panel);
}

void MainWindow::sendDigit(wchar_t digit)
{
    if (!model_.beginDtmf()) return;
    const auto method = model_.selectedDtmfMethod();
    run(controller_->sendDtmf(
        std::string(1, static_cast<char>(digit)),
        method,
        static_cast<unsigned>(durationBox_.Value()),
        static_cast<unsigned>(gapBox_.Value())));
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

void MainWindow::run(std::future<util::Result<void>> operation)
{
    const auto lifetime = lifetime_;
    std::thread([this, lifetime, operation = std::move(operation)]() mutable {
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
    model_.completeCallCommand();
    model_.completeHangupCommand();
    model_.completeDtmf();
    errorText_.Text(to_hstring(error.message));
}

void MainWindow::queueRender(const core::TelephonySnapshot& snapshot)
{
    const auto lifetime = lifetime_;
    lifetime->dispatcher.TryEnqueue([this, lifetime, snapshot] {
        if (!lifetime->closing.load()) render(snapshot);
    });
}

void MainWindow::render(const core::TelephonySnapshot& snapshot)
{
    model_.update(snapshot);
    const auto commands = model_.commands();
    if (captureField_ && captureField_.Items().Size() == 1) {
        for (const auto& device : snapshot.devices) {
            if (device.capture) captureField_.Items().Append(box_value(to_hstring(device.name)));
        }
    }
    if (playbackField_ && playbackField_.Items().Size() == 1) {
        for (const auto& device : snapshot.devices) {
            if (device.playback) playbackField_.Items().Append(box_value(to_hstring(device.name)));
        }
    }
    registrationText_.Text(to_hstring(core::registrationStateText(snapshot.registration)));
    callStatusText_.Text(to_hstring(core::callStateText(snapshot.call)));
    remoteText_.Text(snapshot.maskedRemote.empty()
        ? hstring(L"Destino: —")
        : hstring(L"Destino: ") + to_hstring(snapshot.maskedRemote));
    durationText_.Text(to_hstring(core::formatCallDuration(snapshot.callDurationSeconds)));
    audioText_.Text(to_hstring(core::mediaStateText(snapshot.media)));
    if (!snapshot.friendlyError.empty()) errorText_.Text(to_hstring(snapshot.friendlyError));
    else if (snapshot.call == core::CallState::Idle) errorText_.Text(L"");

    const bool canRegister = commands.registerAccount || commands.unregisterAccount;
    registerButton_.IsEnabled(canRegister);
    registerButton_.Content(box_value(
        commands.unregisterAccount ? hstring(L"Desconectar") : hstring(L"Registrar")));
    callButton_.IsEnabled(commands.makeCall);
    destination_.IsEnabled(commands.makeCall);
    answerButton_.IsEnabled(commands.answerCall);
    rejectButton_.IsEnabled(commands.rejectCall);
    hangupButton_.IsEnabled(commands.hangupCall);
    muteButton_.IsEnabled(commands.mute);
    muteButton_.Content(box_value(snapshot.muted ? L"Desativar mudo" : L"Ativar mudo"));
    keypadButton_.IsEnabled(commands.keypad);
    keypad_.IsHitTestVisible(commands.sendDtmf);
    keypad_.Opacity(commands.sendDtmf ? 1.0 : 0.55);
    dtmfMethod_.IsEnabled(commands.keypad);
    durationBox_.IsEnabled(commands.keypad);
    gapBox_.IsEnabled(commands.keypad);
    ivrToggle_.IsEnabled(commands.keypad);
    if (!commands.keypad) keypad_.Visibility(Visibility::Collapsed);

    std::wostringstream diagnostic;
    diagnostic << L"POLPhone: 0.1.0\n"
               << L"Backend: " << (snapshot.demoMode ? L"Demonstração (mock sem PJSIP/rede)" : L"SIP real") << L'\n'
               << L"PJSIP: " << (snapshot.demoMode ? L"não inicializado pelo backend de demonstração" : L"2.17") << L'\n'
               << L"Endpoint: " << to_hstring(snapshot.endpointState).c_str() << L'\n'
               << L"Registro: " << to_hstring(core::registrationStateText(snapshot.registration)).c_str() << L'\n'
               << L"Chamada: " << to_hstring(core::callStateText(snapshot.call)).c_str() << L'\n'
               << L"Destino: " << (snapshot.maskedRemote.empty() ? L"—" : to_hstring(snapshot.maskedRemote).c_str()) << L'\n'
               << L"Codec: " << (snapshot.codec.empty() ? L"—" : to_hstring(snapshot.codec).c_str()) << L'\n'
               << L"Mídia: " << to_hstring(core::mediaStateText(snapshot.media)).c_str() << L'\n'
               << L"Detalhe técnico: " << (snapshot.technicalDetail.empty()
                    ? L"—" : to_hstring(snapshot.technicalDetail).c_str()) << L'\n'
               << L"DTMF: " << to_hstring(core::dtmfMethodText(snapshot.dtmfMethod)).c_str()
               << L", duração=" << snapshot.dtmfDurationMs << L"ms, intervalo=" << snapshot.dtmfGapMs << L"ms\n"
               << L"Entrada: " << (snapshot.captureDevice ? std::to_wstring(*snapshot.captureDevice) : L"padrão") << L'\n'
               << L"Saída: " << (snapshot.playbackDevice ? std::to_wstring(*snapshot.playbackDevice) : L"padrão") << L"\n\n"
               << L"Últimos logs sanitizados:\n";
    for (const auto& line : snapshot.sanitizedLogs) diagnostic << to_hstring(line).c_str() << L'\n';
    diagnosticsText_.Text(diagnostic.str());
}

} // namespace polphone::gui
