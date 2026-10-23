#requires -Version 7.0
<#
.SYNOPSIS
Compatibility command for packaging the one native Windows launcher.
.DESCRIPTION
The old PowerShell GUI/Start-HLClient.cmd package is retired. This command
does not compile or select a different launcher. It forwards explicit Release
inputs to the canonical audited native packager, producing HLClientEngine-Win10.zip.
PowerShell is developer-side tooling only; the shipped program starts from
HLClientLauncher.exe and needs no scripts.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$AcceptedRunDirectory,
    [switch]$LauncherRebuilt,
    [string]$LauncherChangeDescription,
    [switch]$ClientRebuilt,
    [string]$ClientChangeDescription,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$BaseClientSha256,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ExpectedClientSha256 = 'E044684FE00E688F555988478E83E7DEF037EC35844C05F19E7B540C45CAA32C'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'package_windows10_release.ps1') @PSBoundParameters
