[CmdletBinding()]
param(
    [switch]$DtmfOnly,
    [int]$Tail = 100
)

. (Join-Path $PSScriptRoot "lab-common.ps1")
$context = Get-POLPhoneLabContext
Assert-POLPhoneDocker

Push-Location $context.LabDir
try {
    if ($DtmfOnly) {
        & docker compose logs --follow --tail $Tail asterisk | Select-String -SimpleMatch "POLPHONE_LAB_DTMF"
    } else {
        & docker compose logs --follow --tail $Tail asterisk
    }
} finally {
    Pop-Location
}
