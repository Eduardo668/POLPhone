. (Join-Path $PSScriptRoot "lab-common.ps1")
$exitCode = 1
Invoke-POLPhoneLabWsl -ScriptName "lab-status.sh" -Arguments @($args) -ExitCode ([ref]$exitCode)
exit $exitCode
