[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath)
$executable = Join-Path $repoRoot "build\gui\$Config\polphone.exe"
if (-not (Test-Path $executable -PathType Leaf)) {
    throw "Interface não encontrada: $executable. Rode scripts/build-gui.ps1 -Config $Config."
}
Push-Location $repoRoot
$stagingDirectory = $null
try {
    if ($repoRoot.StartsWith("\\")) {
        $stagingDirectory = Join-Path ([IO.Path]::GetTempPath()) `
            ("POLPhone-demo-" + [Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
        Copy-Item -Path (Join-Path (Split-Path $executable -Parent) "*") `
            -Destination $stagingDirectory -Recurse -Force
        $stagedExecutable = Join-Path $stagingDirectory "polphone.exe"
        $process = Start-Process -FilePath $stagedExecutable -ArgumentList "--demo" `
            -WorkingDirectory $stagingDirectory -Wait -PassThru
        $demoExitCode = $process.ExitCode
    } else {
        & $executable --demo
        $demoExitCode = $LASTEXITCODE
    }
} finally {
    Pop-Location
    if ($stagingDirectory -and (Test-Path $stagingDirectory -PathType Container)) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
}
if ($demoExitCode -ne 0) {
    throw "A interface em modo de demonstração terminou com código $demoExitCode."
}
exit $demoExitCode
