<#
Offline startup-log regression tests under Windows PowerShell 5.1. Only a local
fixture executable is launched: no Steam, game, server, network or user settings.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -ne 5) {
    $ps51 = (Get-Command powershell.exe -ErrorAction Stop).Source
    & $ps51 -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PSCommandPath
    exit $LASTEXITCODE
}

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repository 'portable\Portable-StartupLogging.ps1')
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$fixture = [System.IO.Path]::GetFullPath((Join-Path $tempRoot ('hlc-portable-log-test-' + [guid]::NewGuid().ToString('N'))))
$originalLocalAppData = $env:LOCALAPPDATA
$assertions = 0

function Assert-LogTest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:assertions++
}

function New-TestLog([string]$Root) {
    Initialize-PortableLog -PackageRoot $Root
    Assert-LogTest ([System.IO.Directory]::Exists($script:PortableLogDirectory)) 'Startup log directory was not created.'
    Assert-LogTest ([System.IO.File]::Exists((Join-Path $script:PortableLogDirectory 'launcher.log'))) 'Launcher log was not created before child startup.'
}

function Read-ClientLog {
    return [System.IO.File]::ReadAllText((Join-Path $script:PortableLogDirectory 'client-output.log'))
}

try {
    Assert-LogTest ($fixture.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) 'Fixture escaped the temporary directory.'
    [void][System.IO.Directory]::CreateDirectory($fixture)
    # Keep source ASCII for PowerShell 5.1; construct Unicode names at runtime.
    $unicode = [string][char]0x0416
    $package = Join-Path $fixture ('Portable Client ' + $unicode)
    [void][System.IO.Directory]::CreateDirectory($package)
    $env:LOCALAPPDATA = Join-Path $fixture 'Isolated LocalAppData'
    $child = Join-Path $package ('Startup Fixture ' + $unicode + '.exe')
    Add-Type -OutputAssembly $child -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

public static class PortableLoggingFixture {
    private static Timer watchdog;
    public static int Main(string[] args) {
        watchdog = new Timer(_ => Environment.Exit(99), null, 45000, Timeout.Infinite);
        Console.OutputEncoding = new UTF8Encoding(false);
        if (args.Length == 0) return 90;
        switch (args[0]) {
        case "native":
            Console.WriteLine("fixture stdout started");
            Console.Error.WriteLine("fixture renderer initialization failed");
            return 7;
        case "argv":
            for (int i = 1; i < args.Length; ++i)
                Console.WriteLine("arg64[" + (i - 1) + "]=" + Convert.ToBase64String(Encoding.UTF8.GetBytes(args[i])));
            Console.WriteLine("cwd64=" + Convert.ToBase64String(Encoding.UTF8.GetBytes(Environment.CurrentDirectory)));
            return 0;
        case "durable":
            Console.WriteLine("durable-before-child-exit");
            Console.Out.Flush();
            var until = DateTime.UtcNow.AddSeconds(8);
            while (DateTime.UtcNow < until) {
                try {
                    using (var file = new FileStream(args[1], FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                    using (var reader = new StreamReader(file)) {
                        if (reader.ReadToEnd().Contains("durable-before-child-exit")) {
                            Console.Error.WriteLine("durable-read-confirmed-while-child-alive");
                            return 0;
                        }
                    }
                } catch (IOException) { }
                Thread.Sleep(25);
            }
            return 27;
        case "private":
            Console.WriteLine("password=fixture-private-A");
            Console.WriteLine("Authorization: fixture-private-B");
            Console.Error.WriteLine("Steam_ID=fixture-private-C");
            Console.Error.WriteLine("[chat] fixture-private-D");
            Console.WriteLine("ticket fixture-private-E");
            Console.WriteLine("auth_payload fixture-private-F");
            Console.WriteLine("safe diagnostic remains");
            return 0;
        case "long":
            Console.WriteLine("overlong-private-marker" + new string('L', 20000));
            Console.Write("final-fragment-without-newline");
            Console.Error.Write("stderr-final-fragment");
            return 0;
        case "stress":
            var first = Task.Factory.StartNew(() => {
                for (int i = 0; i < 4000; ++i) Console.WriteLine("output-" + i + ":" + new string('O', 1000));
            });
            var second = Task.Factory.StartNew(() => {
                for (int i = 0; i < 4000; ++i) Console.Error.WriteLine("error-" + i + ":" + new string('E', 1000));
            });
            Task.WaitAll(first, second);
            return 19;
        default: return 91;
        }
    }
}
'@

    New-TestLog $package
    $firstDirectory = $script:PortableLogDirectory
    $result = @(Invoke-PortableClient -Executable $child -Arguments @('native') -WorkingDirectory $package)
    Assert-LogTest ($result.Count -eq 1 -and [int]$result[0] -eq 7) 'Native stderr masked or changed the child exit code.'
    $output = Read-ClientLog
    Assert-LogTest ($output.Contains('[stdout] fixture stdout started')) 'stdout was not captured.'
    Assert-LogTest ($output.Contains('[stderr] fixture renderer initialization failed')) 'Native stderr was not captured.'
    Assert-LogTest ($output.Contains('0x00000007')) 'Native exit code was not retained in hexadecimal.'
    $launcherLog = [System.IO.File]::ReadAllText((Join-Path $script:PortableLogDirectory 'launcher.log'))
    Assert-LogTest ($launcherLog.Contains('Client exit code=7')) 'Launcher log did not retain child failure.'

    New-TestLog $package
    Assert-LogTest ($script:PortableLogDirectory -cne $firstDirectory) 'Repeated launches overwrote a previous log directory.'
    Assert-LogTest ([System.IO.File]::Exists((Join-Path $firstDirectory 'client-output.log'))) 'Previous launch evidence was lost.'
    $values = [string[]]@('', 'two words', ('Unicode ' + $unicode), 'literal "quotes"', 'C:\trailing slash\', '& echo should-not-run', 'a\\"b')
    $exitCode = Invoke-PortableClient -Executable $child -Arguments (@('argv') + $values) -WorkingDirectory $package
    Assert-LogTest ($exitCode -eq 0) 'Argument fixture did not complete.'
    $output = Read-ClientLog
    for ($i = 0; $i -lt $values.Count; $i++) {
        $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($values[$i]))
        Assert-LogTest ($output.Contains(('arg64[{0}]={1}' -f $i, $encoded))) "Argument $i did not survive native quoting."
    }
    $encodedCwd = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($package))
    Assert-LogTest ($output.Contains('cwd64=' + $encodedCwd)) 'Working directory was not preserved.'

    New-TestLog $package
    $exitCode = Invoke-PortableClient -Executable $child -Arguments @('durable', (Join-Path $script:PortableLogDirectory 'client-output.log')) -WorkingDirectory $package
    Assert-LogTest ($exitCode -eq 0) 'A short complete line was not durably flushed while the child remained alive.'
    Assert-LogTest ((Read-ClientLog).Contains('durable-read-confirmed-while-child-alive')) 'Missing independent durable-write witness.'

    New-TestLog $package
    Write-PortableLog 'password=fixture-private-launcher'
    $exitCode = Invoke-PortableClient -Executable $child -Arguments @('private') -WorkingDirectory $package
    Assert-LogTest ($exitCode -eq 0) 'Sanitization fixture did not complete.'
    $output = Read-ClientLog
    Assert-LogTest (-not $output.Contains('fixture-private-')) 'Sensitive or chat output was saved.'
    Assert-LogTest ($output.Contains('[sensitive or chat line omitted]')) 'Redaction was not explicit.'
    Assert-LogTest ($output.Contains('safe diagnostic remains')) 'Sanitization removed ordinary failure diagnostics.'
    $launcherLog = [System.IO.File]::ReadAllText((Join-Path $script:PortableLogDirectory 'launcher.log'))
    Assert-LogTest (-not $launcherLog.Contains('fixture-private-launcher')) 'Launcher metadata was not sanitized.'

    New-TestLog $package
    $exitCode = Invoke-PortableClient -Executable $child -Arguments @('long') -WorkingDirectory $package
    Assert-LogTest ($exitCode -eq 0) 'Long-line fixture did not complete.'
    $output = Read-ClientLog
    Assert-LogTest ($output.Contains('[overlong line omitted]') -and -not $output.Contains('overlong-private-marker')) 'Oversized lines were not omitted atomically.'
    Assert-LogTest ($output.Contains('final-fragment-without-newline') -and $output.Contains('stderr-final-fragment')) 'EOF fragments were lost.'

    New-TestLog $package
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $exitCode = Invoke-PortableClient -Executable $child -Arguments @('stress') -WorkingDirectory $package
    $watch.Stop()
    Assert-LogTest ($exitCode -eq 19) 'Concurrent over-capacity stdout/stderr was not completely drained.'
    Assert-LogTest ($watch.Elapsed.TotalSeconds -lt 45) 'Bounded capture stalled while draining output.'
    $outputPath = Join-Path $script:PortableLogDirectory 'client-output.log'
    Assert-LogTest ((Get-Item -LiteralPath $outputPath).Length -le (2 * 1024 * 1024)) 'Client log exceeded its 2 MiB cap.'
    Assert-LogTest ((Read-ClientLog).Contains('[output limit reached:')) 'Capped output has no explicit truncation marker.'
    $launcherLog = [System.IO.File]::ReadAllText((Join-Path $script:PortableLogDirectory 'launcher.log'))
    Assert-LogTest ($launcherLog.Contains('Client exit code=19')) 'Output saturation lost the independent final status.'

    New-TestLog $package
    $launcherPath = Join-Path $script:PortableLogDirectory 'launcher.log'
    for ($i = 0; $i -lt 100; $i++) { Write-PortableLog ('bounded launcher record ' + ('M' * 6000)) }
    Assert-LogTest ((Get-Item -LiteralPath $launcherPath).Length -le (262144 + 8192)) 'Launcher log growth was not bounded.'
    $fullLength = (Get-Item -LiteralPath $launcherPath).Length
    Write-PortableLog 'must not extend saturated launcher log'
    Assert-LogTest ((Get-Item -LiteralPath $launcherPath).Length -eq $fullLength) 'Saturated launcher log continued growing.'

    New-TestLog $package
    $launcherPath = Join-Path $script:PortableLogDirectory 'launcher.log'
    $lockedLog = [System.IO.File]::Open($launcherPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    try {
        $warnings = @(Write-PortableLog 'write intentionally blocked by local fixture' 3>&1)
        Assert-LogTest ($warnings.Count -gt 0) 'A runtime log-write failure was silently hidden.'
    } finally { $lockedLog.Dispose() }

    New-TestLog $package
    $launchFailed = $false
    try {
        $null = Invoke-PortableClient -Executable (Join-Path $package 'missing-fixture.exe') -Arguments @() -WorkingDirectory $package
    } catch { $launchFailed = $true }
    Assert-LogTest $launchFailed 'Missing executable was reported as a successful launch.'
    $launcherLog = [System.IO.File]::ReadAllText((Join-Path $script:PortableLogDirectory 'launcher.log'))
    Assert-LogTest ($launcherLog.Contains('Client launch failed:')) 'Executable startup failure was not retained.'
    Assert-LogTest ($launcherLog.Contains('System.ComponentModel.Win32Exception') -and $launcherLog.Contains('Windows native error code=2')) 'Missing executable did not retain the specific Windows file-not-found reason.'

    $blockedPackage = Join-Path $fixture 'Blocked Package'
    [void][System.IO.Directory]::CreateDirectory($blockedPackage)
    [System.IO.File]::WriteAllText((Join-Path $blockedPackage 'logs'), 'fixture blocks log directory')
    New-TestLog $blockedPackage
    $fallbackPrefix = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'HLClientEngine\portable-logs')).TrimEnd('\') + '\'
    Assert-LogTest ($script:PortableLogDirectory.StartsWith($fallbackPrefix, [StringComparison]::OrdinalIgnoreCase)) 'Unwritable package did not select isolated LocalAppData fallback.'
    Write-PortableLog 'fallback write succeeds'
    Assert-LogTest ([System.IO.File]::ReadAllText((Join-Path $script:PortableLogDirectory 'launcher.log')).Contains('fallback write succeeds')) 'Fallback log was not writable.'

    # Both destinations deliberately unavailable; fail visibly, without silently
    # starting a client whose failure could not be copied from the other PC.
    $env:LOCALAPPDATA = Join-Path $fixture 'Blocked LocalAppData'
    [System.IO.File]::WriteAllText($env:LOCALAPPDATA, 'fixture blocks fallback directory')
    $loggingFailed = $false
    try { Initialize-PortableLog -PackageRoot $blockedPackage } catch { $loggingFailed = $true }
    Assert-LogTest ($loggingFailed -and -not $script:PortableLogDirectory) 'Unavailable logging roots were not reported explicitly.'
    Write-Host "PASS portable startup logs: $assertions assertions; Windows PowerShell 5.1; native stderr, exit codes, Unicode argv, durable output, redaction, bounded drains, fallback and startup failures."
}
finally {
    $env:LOCALAPPDATA = $originalLocalAppData
    if (Test-Path -LiteralPath $fixture) {
        $resolved = [System.IO.Path]::GetFullPath($fixture)
        if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to delete fixture outside temporary directory: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
