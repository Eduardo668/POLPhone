[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot "lab-common.ps1")
$context = Get-POLPhoneLabContext
Assert-POLPhoneDocker

Push-Location $context.LabDir
try {
    Invoke-POLPhoneDocker -Arguments @("compose", "ps")
    Invoke-POLPhoneDocker -Arguments @("compose", "exec", "-T", "asterisk", "asterisk", "-rx", "core show version")
    Invoke-POLPhoneDocker -Arguments @("compose", "exec", "-T", "asterisk", "asterisk", "-rx", "module show like chan_sip.so")
    Invoke-POLPhoneDocker -Arguments @("compose", "exec", "-T", "asterisk", "asterisk", "-rx", "module show like chan_pjsip.so")
    Invoke-POLPhoneDocker -Arguments @("compose", "exec", "-T", "asterisk", "asterisk", "-rx", "sip show peers")
    Invoke-POLPhoneDocker -Arguments @("compose", "exec", "-T", "asterisk", "asterisk", "-rx", "dialplan show from-lab")
} finally {
    Pop-Location
}
