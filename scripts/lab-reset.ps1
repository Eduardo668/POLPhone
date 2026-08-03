[CmdletBinding()]
param(
    [switch]$ConfirmReset,
    [switch]$RemoveEnv
)

. (Join-Path $PSScriptRoot "lab-common.ps1")
$context = Get-POLPhoneLabContext
Assert-POLPhoneDocker

if (-not $ConfirmReset) {
    $confirmation = Read-Host "Digite RESETAR-LAB para remover containers, volumes e dados transitórios"
    if ($confirmation -cne "RESETAR-LAB") {
        Write-Host "Reset cancelado; nenhuma alteração realizada."
        exit 0
    }
}

Push-Location $context.LabDir
try {
    Invoke-POLPhoneDocker -Arguments @("compose", "down", "--volumes", "--remove-orphans")
} finally {
    Pop-Location
}

$dataDir = Join-Path $context.LabDir "data"
if (Test-Path -LiteralPath $dataDir -PathType Container) {
    Get-ChildItem -LiteralPath $dataDir -Force | Where-Object { $_.Name -ne ".gitkeep" } | Remove-Item -Recurse -Force
}

if ($RemoveEnv -and (Test-Path -LiteralPath $context.EnvFile)) {
    $envConfirmation = Read-Host "Digite REMOVER-ENV para apagar também os segredos locais"
    if ($envConfirmation -ceq "REMOVER-ENV") {
        Remove-Item -LiteralPath $context.EnvFile -Force
        Write-Host "Configuração local .env removida; será necessário executar lab-init.ps1 novamente."
    } else {
        Write-Host ".env preservado."
    }
}

Write-Host "Laboratório resetado; imagens locais não foram publicadas nem removidas."
