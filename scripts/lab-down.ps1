[CmdletBinding()]
param(
    [switch]$RemoveVolumes
)

. (Join-Path $PSScriptRoot "lab-common.ps1")
$context = Get-POLPhoneLabContext
Assert-POLPhoneDocker

Push-Location $context.LabDir
try {
    $arguments = @("compose", "down", "--remove-orphans")
    if ($RemoveVolumes) { $arguments += "--volumes" }
    Invoke-POLPhoneDocker -Arguments $arguments
} finally {
    Pop-Location
}
