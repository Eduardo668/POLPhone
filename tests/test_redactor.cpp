/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#include "logging/Logger.h"
#include "logging/PjLogWriter.h"
#include "logging/Redactor.h"
#include "util/Time.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace polphone::logging;

namespace {

constexpr std::string_view kSyntheticSecret = "T05_ONLY_SYNTHETIC_SECRET";
constexpr std::string_view kSyntheticPhone = "999999991234";

class MemorySink final : public LogSink {
public:
    polphone::util::Result<void> write(std::string_view line, LogLevel) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.emplace_back(line);
        return polphone::util::Result<void>::success();
    }

    polphone::util::Result<void> flush() override
    {
        return polphone::util::Result<void>::success();
    }

    std::vector<std::string> lines() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

class FailingSink final : public LogSink {
public:
    polphone::util::Result<void> write(std::string_view, LogLevel) override
    {
        return polphone::util::Result<void>::failure(
            polphone::util::ErrorCode::Io, "falha sintética");
    }
    polphone::util::Result<void> flush() override
    {
        return polphone::util::Result<void>::failure(
            polphone::util::ErrorCode::Io, "falha sintética");
    }
};

class ThrowingSink final : public LogSink {
public:
    polphone::util::Result<void> write(std::string_view, LogLevel) override
    {
        throw std::runtime_error("falha sintética do sink");
    }
    polphone::util::Result<void> flush() override
    {
        throw std::runtime_error("falha sintética do sink");
    }
};

class TemporaryPath final {
public:
    TemporaryPath()
    {
        static std::atomic<unsigned> sequence{0};
        path_ = std::filesystem::temp_directory_path()
            / ("polphone-t05-" + std::to_string(polphone::util::monotonicMilliseconds())
               + "-" + std::to_string(sequence.fetch_add(1)));
    }

    ~TemporaryPath()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& get() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::string readAll(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST_SUITE("redactor") {
    TEST_CASE("conversão de níveis preserva a escala do PJSIP")
    {
        CHECK(logLevelFromNumber(0) == LogLevel::Off);
        CHECK(logLevelFromNumber(1) == LogLevel::Fatal);
        CHECK(logLevelFromNumber(3) == LogLevel::Warning);
        CHECK(logLevelFromNumber(5) == LogLevel::Debug);
        CHECK(logLevelFromNumber(99) == LogLevel::Trace);
        CHECK(logLevelName(LogLevel::Info) == "INFO ");
    }

    TEST_CASE("formatação básica contém timestamp nível componente contexto e id")
    {
        const LogEntry entry{"2026-07-31T12:00:00.000-03:00", LogLevel::Info,
                             "sip", "mensagem", "contexto", "test-0001"};
        const std::string line = formatLogEntry(entry);
        CHECK(line == "2026-07-31T12:00:00.000-03:00 [INFO ] [sip] "
                      "id=test-0001 mensagem | contexto");
    }

    TEST_CASE("senha em texto simples é removida integralmente")
    {
        const std::string result = Redactor::redact(
            "password=T05_ONLY_SYNTHETIC_SECRET passwd:outro secret = terceiro");
        CHECK(result.find(kSyntheticSecret) == std::string::npos);
        CHECK(result.find("outro") == std::string::npos);
        CHECK(result.find("terceiro") == std::string::npos);
        CHECK(result.find("[REDACTED]") != std::string::npos);
    }

    TEST_CASE("senha em JSON é removida preservando a chave")
    {
        const std::string result = Redactor::redact(
            R"({"password": "T05_ONLY_SYNTHETIC_SECRET", "mode": "test"})");
        CHECK(result == R"({"password": "[REDACTED]", "mode": "test"})");
    }

    TEST_CASE("Authorization é removido como cabeçalho completo")
    {
        const std::string result = Redactor::redact(
            R"(Authorization: Digest username="test", response="T05_ONLY_SYNTHETIC_SECRET")");
        CHECK(result == "Authorization: [REDACTED]");
    }

    TEST_CASE("Proxy-Authorization é removido como cabeçalho completo")
    {
        const std::string result = Redactor::redact(
            "Proxy-Authorization : Digest T05_ONLY_SYNTHETIC_SECRET");
        CHECK(result == "Proxy-Authorization : [REDACTED]");
    }

    TEST_CASE("nonce cnonce e response Digest são removidos fora de cabeçalho")
    {
        const std::string result = Redactor::redact(
            R"(nonce="nonce-test", cnonce=cnonce-test, response="response-test")");
        CHECK(result.find("nonce-test") == std::string::npos);
        CHECK(result.find("cnonce-test") == std::string::npos);
        CHECK(result.find("response-test") == std::string::npos);
        CHECK(result.find("nonce=\"[REDACTED]\"") != std::string::npos);
    }

    TEST_CASE("URI SIP externa preserva somente quatro dígitos")
    {
        CHECK(Redactor::redact("sip:999999991234@example.invalid")
              == "sip:********1234@example.invalid");
        CHECK(Redactor::redact("sips:usuario@example.invalid")
              == "sips:*******@example.invalid");
    }

    TEST_CASE("ramal interno em From To ou Contact permanece legível")
    {
        CHECK(Redactor::redact("From: <sip:1001@example.invalid>;tag=abcdef")
              == "From: <sip:1001@example.invalid>;tag=abcdef");
    }

    TEST_CASE("número telefônico contextual preserva somente quatro dígitos")
    {
        CHECK(Redactor::redact("destino=999999991234") == "destino=********1234");
        CHECK(Redactor::redact("999999991234") == "********1234");
    }

    TEST_CASE("Call-ID User-Agent IP e texto técnico permanecem legíveis")
    {
        CHECK(Redactor::redact("Call-ID: 999999991234@example.invalid")
              == "Call-ID: 999999991234@example.invalid");
        CHECK(Redactor::redact("User-Agent: POLPhone/0.1.0")
              == "User-Agent: POLPhone/0.1.0");
        CHECK(Redactor::redact("transport UDP 192.0.2.10:5060 ativo")
              == "transport UDP 192.0.2.10:5060 ativo");
    }

    TEST_CASE("redaction é case-insensitive")
    {
        const std::string result = Redactor::redact(
            "PaSsWoRd=T05_ONLY_SYNTHETIC_SECRET NONCE=nonce-test");
        CHECK(result.find(kSyntheticSecret) == std::string::npos);
        CHECK(result.find("nonce-test") == std::string::npos);
    }

    TEST_CASE("redaction é idempotente")
    {
        const std::string once = Redactor::redact(
            R"(password="T05_ONLY_SYNTHETIC_SECRET" sip:999999991234@example.invalid)");
        CHECK(Redactor::redact(once) == once);
    }

    TEST_CASE("mensagem vazia permanece vazia")
    {
        CHECK(Redactor::redact("").empty());
    }

    TEST_CASE("caracteres UTF-8 não sensíveis permanecem legíveis")
    {
        CHECK(Redactor::redact("inicialização concluída — áudio não iniciado")
              == "inicialização concluída — áudio não iniciado");
    }

    TEST_CASE("caminhos de máquina não expõem o usuário")
    {
        const std::string wsl = Redactor::redact(
            R"(stack by \wsl.localhost\Ubuntu\home\usuario-local\Projects\POLPhone\src\main.cpp:10)");
        CHECK(wsl == R"(stack by POLPhone\src\main.cpp:10)");
        CHECK(wsl.find("usuario-local") == std::string::npos);

        const std::string windows = Redactor::redact(
            R"(arquivo C:\Users\usuario-local\AppData\Local\trace.log)");
        CHECK(windows == R"(arquivo C:\Users\[USER]\AppData\Local\trace.log)");

        const std::string multiple = Redactor::redact(
            R"(POLPhone\src\main.cpp e \wsl.localhost\Ubuntu\home\usuario-local\Projects\POLPhone\src\other.cpp)");
        CHECK(multiple == R"(POLPhone\src\main.cpp e POLPhone\src\other.cpp)");
    }

    TEST_CASE("dígitos DTMF são mascarados por padrão e opt-in é respeitado")
    {
        CHECK(Redactor::redact("Signal=7 digit=9") == "Signal=* digit=*");
        CHECK(Redactor::redact("Signal=7 digit=9", true) == "Signal=7 digit=9");
    }

    TEST_CASE("Logger sanitiza antes de escrever no sink")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Trace));
        CHECK(logger.info("test", "password=T05_ONLY_SYNTHETIC_SECRET"));
        const auto lines = memory->lines();
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].find(kSyntheticSecret) == std::string::npos);
        CHECK(lines[0].find("[REDACTED]") != std::string::npos);
    }

    TEST_CASE("Logger altera o nível de um sink em runtime")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Info));
        CHECK(logger.debug("test", "antes"));
        CHECK(memory->lines().empty());
        REQUIRE(logger.setSinkLevel(memory, LogLevel::Debug));
        CHECK(logger.debug("test", "depois"));
        REQUIRE(memory->lines().size() == 1);
        CHECK(memory->lines()[0].find("depois") != std::string::npos);
        CHECK_FALSE(logger.setSinkLevel(std::make_shared<MemorySink>(), LogLevel::Trace));
    }

    TEST_CASE("Logger só preserva dígitos DTMF após opt-in explícito")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Info));
        CHECK(logger.info("dtmf", "digit=7"));
        logger.setLogDtmfDigits(true);
        CHECK(logger.info("dtmf", "digit=8"));
        const auto lines = memory->lines();
        REQUIRE(lines.size() == 2U);
        CHECK(lines[0].find("digit=*") != std::string::npos);
        CHECK(lines[1].find("digit=8") != std::string::npos);
    }

    TEST_CASE("arquivo temporário recebe somente conteúdo sanitizado")
    {
        TemporaryPath temporary;
        Logger logger;
        const auto file = logger.enableFile(temporary.get(), LogLevel::Debug);
        REQUIRE(file.hasValue());
        CHECK(logger.info("test", "token=T05_ONLY_SYNTHETIC_SECRET"));
        CHECK(logger.flush());
        const std::string content = readAll(file.value());
        CHECK(content.find(kSyntheticSecret) == std::string::npos);
        CHECK(content.find("[REDACTED]") != std::string::npos);
    }

    TEST_CASE("arquivo desabilitado não cria diretório")
    {
        TemporaryPath temporary;
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Info));
        CHECK(logger.info("test", "somente memória"));
        CHECK_FALSE(std::filesystem::exists(temporary.get()));
    }

    TEST_CASE("falha de abertura do arquivo retorna erro sem crash")
    {
        TemporaryPath temporary;
        std::filesystem::create_directories(temporary.get());
        const auto blockingFile = temporary.get() / "nao-e-diretorio";
        std::ofstream(blockingFile) << "bloqueio sintético";
        const auto result = FileLogSink::create(blockingFile / "logs");
        CHECK(result.hasError());
        CHECK(result.error().code == polphone::util::ErrorCode::Io);
    }

    TEST_CASE("múltiplas threads não misturam linhas")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Info));
        std::vector<std::thread> threads;
        for (int thread = 0; thread < 8; ++thread) {
            threads.emplace_back([&logger, thread] {
                for (int item = 0; item < 50; ++item) {
                    static_cast<void>(logger.info(
                        "thread", "thread=" + std::to_string(thread)
                                      + " item=" + std::to_string(item)));
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        const auto lines = memory->lines();
        REQUIRE(lines.size() == 400);
        CHECK(std::all_of(lines.begin(), lines.end(), [](const std::string& line) {
            return line.find('\n') == std::string::npos
                && line.find("thread=") != std::string::npos
                && line.find("item=") != std::string::npos;
        }));
    }

    TEST_CASE("falha de sink não impede os demais destinos")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Info));
        REQUIRE(logger.addSink(std::make_shared<FailingSink>(), LogLevel::Info));
        CHECK_FALSE(logger.info("test", "mensagem segura"));
        CHECK(memory->lines().size() == 1);
    }

    TEST_CASE("exceção de sink não atravessa a interface do Logger")
    {
        Logger logger;
        REQUIRE(logger.addSink(std::make_shared<ThrowingSink>(), LogLevel::Info));
        CHECK_FALSE(logger.info("test", "mensagem segura"));
        CHECK_FALSE(logger.flush());
    }

    TEST_CASE("mapeamento do adaptador PJSIP não colapsa níveis")
    {
        CHECK(PjLogWriter::mapLevel(1) == LogLevel::Fatal);
        CHECK(PjLogWriter::mapLevel(2) == LogLevel::Error);
        CHECK(PjLogWriter::mapLevel(4) == LogLevel::Info);
        CHECK(PjLogWriter::mapLevel(5) == LogLevel::Debug);
        CHECK(PjLogWriter::mapLevel(6) == LogLevel::Trace);
    }

    TEST_CASE("log PJSIP fictício é normalizado e identificado")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Trace));
        PjLogWriter writer(logger);
        pj::LogEntry entry{4, "primeira\r\nsegunda\r\n", 1, "test-thread"};
        writer.write(entry);
        const auto lines = memory->lines();
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].find("[pjsip]") != std::string::npos);
        CHECK(lines[0].find("primeira") != std::string::npos);
        CHECK(lines[1].find("segunda") != std::string::npos);
    }

    TEST_CASE("log PJSIP converte texto ANSI do Windows para UTF-8")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Trace));
        PjLogWriter writer(logger);
        std::string nativeText = "Sa";
        nativeText.push_back(static_cast<char>(0xED)); // í em Windows-1252.
        nativeText += "da Digital";
        pj::LogEntry entry{4, nativeText, 1, "test-thread"};
        writer.write(entry);
        const auto lines = memory->lines();
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].find("Saída Digital") != std::string::npos);
    }

    TEST_CASE("credencial de log PJSIP fictício chega sanitizada")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Trace));
        PjLogWriter writer(logger);
        pj::LogEntry entry{
            5,
            "Authorization: Digest response=\"T05_ONLY_SYNTHETIC_SECRET\"\n"
            "To: <sip:999999991234@example.invalid>\n",
            1,
            "test-thread"};
        writer.write(entry);
        const auto lines = memory->lines();
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].find(kSyntheticSecret) == std::string::npos);
        CHECK(lines[0].find("Authorization: [REDACTED]") != std::string::npos);
        CHECK(lines[1].find(kSyntheticPhone) == std::string::npos);
        CHECK(lines[1].find("****1234") != std::string::npos);
    }

    TEST_CASE("writer é destruído antes do Logger no uso não transferido")
    {
        Logger logger;
        auto memory = std::make_shared<MemorySink>();
        REQUIRE(logger.addSink(memory, LogLevel::Info));
        {
            PjLogWriter writer(logger);
            pj::LogEntry entry{4, "ciclo de vida", 1, "test-thread"};
            writer.write(entry);
        }
        CHECK(logger.info("test", "logger ainda ativo após o writer"));
        CHECK(memory->lines().size() == 2);
    }

    TEST_CASE("rotação por tamanho mantém backups limitados")
    {
        TemporaryPath temporary;
        const auto created = FileLogSink::create(temporary.get(), 32U);
        REQUIRE(created.hasValue());
        const auto sink = created.value();
        CHECK(sink->write("linha que ultrapassa o limite configurado", LogLevel::Info));
        CHECK(sink->write("segunda linha que força nova rotação", LogLevel::Info));
        CHECK(sink->flush());
        CHECK(std::filesystem::exists(sink->currentPath()));
        CHECK(std::filesystem::exists(sink->currentPath().string() + ".1"));
    }
}
