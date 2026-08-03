Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

function Get-POLPhoneLabContext {
    $repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
    $labDir = Join-Path $repoRoot "lab\asterisk"
    [pscustomobject]@{
        RepoRoot = $repoRoot
        LabDir = $labDir
        EnvFile = Join-Path $labDir ".env"
        ContainerName = "polphone-asterisk-lab"
        GatewayContainerName = "polphone-asterisk-lab-gateway"
    }
}

function Assert-POLPhoneDocker {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "Docker não foi encontrado no PATH. Instale/inicie o Docker Desktop antes de continuar."
    }
    & docker version --format "{{.Server.Version}}" *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "O daemon Docker não está acessível. Inicie o Docker Desktop."
    }
    & docker compose version *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker Compose v2+ não está disponível."
    }
}

function Invoke-POLPhoneDocker {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    Write-Host "> docker $($Arguments -join ' ')"
    & docker @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "docker $($Arguments -join ' ') falhou com exit code $LASTEXITCODE."
    }
}

function Read-POLPhoneLabEnv {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Configuração local ausente: $Path. Rode scripts/lab-init.ps1."
    }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) { continue }
        $separator = $trimmed.IndexOf("=")
        if ($separator -lt 1) { continue }
        $values[$trimmed.Substring(0, $separator)] = $trimmed.Substring($separator + 1)
    }
    return $values
}

function Assert-POLPhoneLabEnv {
    param([Parameter(Mandatory = $true)][hashtable]$Values)

    $required = @(
        "LAB_BIND_IP", "LAB_ADVERTISED_IP", "LAB_CHAN_SIP_HOST_PORT",
        "LAB_PJSIP_HOST_PORT", "LAB_RTP_START", "LAB_RTP_END",
        "LAB_SIP_1001_SECRET", "LAB_SIP_1002_SECRET", "LAB_PJSIP_2001_SECRET"
    )
    foreach ($name in $required) {
        if (-not $Values.ContainsKey($name) -or [string]::IsNullOrWhiteSpace($Values[$name])) {
            throw "Variável obrigatória ausente no .env: $name"
        }
    }
    if (@($Values.Values | Where-Object { $_ -eq "GENERATE_WITH_LAB_INIT" }).Count -gt 0) {
        throw "O .env ainda contém placeholders. Rode scripts/lab-init.ps1."
    }
    if ($Values["LAB_BIND_IP"] -eq "0.0.0.0" -or $Values["LAB_ADVERTISED_IP"] -eq "0.0.0.0") {
        throw "0.0.0.0 não é aceito pelo laboratório. Use localhost ou um IP local explícito."
    }
}
