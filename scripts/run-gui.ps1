[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$outputDirectory = Join-Path $repoRoot "build\gui\$Config"
$executable = Join-Path $outputDirectory "polphone.exe"
$sourceConfig = Join-Path $repoRoot "config\polphone.config.json"

if (-not (Test-Path $executable -PathType Leaf)) {
    throw "Interface não encontrada: $executable. Rode scripts/build-gui.ps1 -Config $Config."
}
if (-not (Test-Path $sourceConfig -PathType Leaf)) {
    throw "Arquivo de configuração não encontrado: $sourceConfig"
}

$stage = Join-Path ([IO.Path]::GetTempPath()) ("POLPhone-runtime-" + [Guid]::NewGuid().ToString("N"))
$stageConfig = Join-Path $stage "config\polphone.config.json"
New-Item -ItemType Directory -Path (Split-Path $stageConfig -Parent) -Force | Out-Null
Copy-Item -Path (Join-Path $outputDirectory "*") -Destination $stage -Recurse -Force
Copy-Item -LiteralPath $sourceConfig -Destination $stageConfig -Force

Push-Location $stage
try {
    $process = Start-Process -FilePath (Join-Path $stage "polphone.exe") `
        -ArgumentList @("--config", "config\polphone.config.json") `
        -WorkingDirectory $stage -PassThru
    $lastWrite = (Get-Item -LiteralPath $stageConfig).LastWriteTimeUtc

    while (-not $process.HasExited) {
        Start-Sleep -Milliseconds 500
        $currentWrite = (Get-Item -LiteralPath $stageConfig).LastWriteTimeUtc
        if ($currentWrite -ne $lastWrite) {
            Copy-Item -LiteralPath $stageConfig -Destination $sourceConfig -Force
            $lastWrite = $currentWrite
        }
        $process.Refresh()
    }

    if (Test-Path $stageConfig -PathType Leaf) {
        Copy-Item -LiteralPath $stageConfig -Destination $sourceConfig -Force
    }
} finally {
    Pop-Location
}

exit $process.ExitCode
