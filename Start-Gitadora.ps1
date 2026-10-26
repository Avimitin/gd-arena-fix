param(
    [Parameter(Mandatory=$true)][string]$Contents,
    [ValidateSet('60','120')][string]$Mode = '60',
    [string]$ServerUrl = '',
    [switch]$CheckConfig,
    [switch]$Diagnostics,
    [string[]]$ExtraSpiceArguments = @()
)
$ErrorActionPreference = 'Stop'
$directory = (Resolve-Path -LiteralPath $Contents).Path
$spice = Join-Path $directory 'spice64.exe'
$dll = Join-Path $PSScriptRoot 'gd-display-mod.dll'
if (!(Test-Path -LiteralPath $dll)) { $dll = Join-Path $PSScriptRoot 'dist\gd-display-mod.dll' }
if (!(Test-Path -LiteralPath $dll) -or !(Test-Path -LiteralPath $spice)) { throw 'Build the mod and specify the contents directory containing spice64.exe.' }
if (!$CheckConfig -and (Get-Process -Name spice64 -ErrorAction SilentlyContinue)) { throw 'An existing spice64 process is running. Exit that game before starting another instance.' }
$experimental = if ($Mode -eq '120') { '1' } else { '0' }
# Spice may relaunch through UAC without inheriting this process's environment.
# Persist the selected mode beside the DLL; do not restore it when the first
# launcher process exits, because the relaunched game may not have read it yet.
$ini = [IO.Path]::ChangeExtension($dll, '.ini')
$diagnosticValue = if ($Diagnostics) { '1' } else { '0' }
# Update only launcher-owned keys, preserving DisableLive2D and other INI
# preferences/comments. Use the same Windows INI parser as the native DLL.
if (-not ('GdModIni' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class GdModIni {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern uint GetPrivateProfileInt(string section, string key, int fallback, string path);
}
'@
}
$profile = [ordered]@{ Width='1920'; Height='1080'; Refresh=$Mode; FPS=$Mode; Experimental=$experimental; Diagnostics=$diagnosticValue }
foreach ($key in $profile.Keys) {
    if (![GdModIni]::WritePrivateProfileString('display', $key, $profile[$key], $ini)) {
        throw "Cannot save mod INI setting: $key"
    }
}
$live2dValue = if ([GdModIni]::GetPrivateProfileInt('display', 'DisableLive2D', 0, $ini)) { '1' } else { '0' }
Write-Output "Mod profile saved: 1920x1080 @ $Mode Hz, $Mode FPS, Experimental=$experimental, DisableLive2D=$live2dValue"
$names = @('GDMOD_Width','GDMOD_Height','GDMOD_Refresh','GDMOD_FPS','GDMOD_Experimental','GDMOD_Diagnostics','GDMOD_DisableLive2D')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
Push-Location -LiteralPath $directory
try {
    $env:GDMOD_Width = '1920'
    $env:GDMOD_Height = '1080'
    $env:GDMOD_Refresh = $Mode
    $env:GDMOD_FPS = $Mode
    $env:GDMOD_Experimental = $experimental
    $env:GDMOD_Diagnostics = $diagnosticValue
    $env:GDMOD_DisableLive2D = $live2dValue
    if ($CheckConfig) {
        $test = Join-Path ([IO.Path]::GetDirectoryName($dll)) 'gd-mod-test.exe'
        & $test --check-config $Mode $live2dValue
        if ($LASTEXITCODE -ne 0) { throw 'Native configuration check failed with environment variables.' }
        foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
        & $test --check-config $Mode $live2dValue
        if ($LASTEXITCODE -ne 0) { throw 'Native configuration check failed without environment variables.' }
        Write-Output 'PASS: selected mode survives loss of inherited environment; no game launched.'
        return
    }
    $arguments = @('-e','prop/ea3-config-gf.xml','-a','prop/app-config-jc.xml','-v','prop/avs-config-jc.xml')
    $arguments += $ExtraSpiceArguments
    if ($ServerUrl) { $arguments += @('-url', $ServerUrl) }
    $arguments += @('-graphics-force-refresh',$Mode,'-k',$dll)
    & $spice @arguments
    if ($LASTEXITCODE -ne 0) { Write-Warning "spice64 exited with code $LASTEXITCODE" }
} finally {
    Pop-Location
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
