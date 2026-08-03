[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string[]]$AppArguments = @("--version")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$executable = Join-Path $repoRoot "build\$Config\polphone_cli.exe"
$logsDirectory = Join-Path $repoRoot "logs"

if (-not (Test-Path $executable -PathType Leaf)) {
    throw "Executável não encontrado: $executable. Rode scripts/build.ps1 -Config $Config."
}
if (-not (Test-Path $logsDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $logsDirectory | Out-Null
}

Push-Location $repoRoot
try {
    & $executable @AppArguments
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $exitCode
