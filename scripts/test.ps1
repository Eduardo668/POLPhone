[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "Both")]
    [string]$Config = "Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$configs = if ($Config -eq "Both") { @("Debug", "Release") } else { @($Config) }

foreach ($current in $configs) {
    $relative = "build\$current\polphone_tests.exe"
    $executable = Join-Path $repoRoot $relative
    if (-not (Test-Path $executable -PathType Leaf)) {
        throw "Testes $current não encontrados. Rode scripts/build.ps1 -Config $current -Tests."
    }
    Write-Host "> $executable"
    if ($repoRoot.StartsWith("\\")) {
        $commandLine = 'pushd "' + $repoRoot + '" && "' + $relative + '" && popd'
        & cmd.exe /d /s /c $commandLine
    } else {
        & $executable
    }
    if ($LASTEXITCODE -ne 0) { throw "Testes $current falharam com exit code $LASTEXITCODE." }
}

exit 0
