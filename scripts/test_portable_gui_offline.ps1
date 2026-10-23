<#
Runs the portable launcher preflight under Windows PowerShell 5.1 against an
isolated fake Half-Life installation. It must not start Steam, the client, or
a network connection.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $repository 'portable\Start-HLClient.ps1'
$ps51 = (Get-Command powershell.exe -ErrorAction Stop).Source
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$fixture = Join-Path $tempRoot ('hlc-portable-gui-smoke-' + [guid]::NewGuid().ToString('N'))
$fixture = [System.IO.Path]::GetFullPath($fixture)
if (-not $fixture.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing a fixture outside the temporary directory: $fixture"
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-ArgumentValue([string[]]$Vector, [string]$Flag) {
    $indices = @(0..($Vector.Count - 1) | Where-Object { $Vector[$_] -ceq $Flag })
    Assert-True ($indices.Count -eq 1 -and $indices[0] -lt $Vector.Count - 1) "Missing or duplicate argument: $Flag"
    return $Vector[$indices[0] + 1]
}

try {
    # Keep this source ASCII so Windows PowerShell 5.1 can read it without a BOM.
    # Construct a Cyrillic directory name at runtime to test Unicode paths.
    $unicodeSuffix = [string][char]0x0416
    $launcher = Join-Path $fixture ('Portable HL Client ' + $unicodeSuffix)
    $game = Join-Path $fixture ('Steam Library\Half-Life ' + $unicodeSuffix)
    New-Item -ItemType Directory -Path $launcher, (Join-Path $game 'valve') -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $launcher 'Start-HLClient.ps1')
    New-Item -ItemType File -Path (Join-Path $launcher 'hlclient.exe'),
        (Join-Path $launcher 'SDL3.dll'), (Join-Path $game 'steam_api.dll') -Force | Out-Null

    $launcherScript = Join-Path $launcher 'Start-HLClient.ps1'
    $validArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcherScript,
        '-CheckOnly', '-HalfLifePath', $game, '-Server', '127.0.0.1:27015')
    $validOutput = @(& $ps51 @validArguments 2>&1)
    $validExit = $LASTEXITCODE
    Assert-True ($validExit -eq 0) "PowerShell 5.1 CheckOnly failed: $($validOutput -join ' ')"
    Assert-True (($validOutput -join [Environment]::NewLine) -match '127\.0\.0\.1:27015') 'CheckOnly did not report the selected server.'

    $invalidArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcherScript,
        '-CheckOnly', '-HalfLifePath', $game, '-Server', '127.0.0.1:0')
    $invalidOutput = @(& $ps51 @invalidArguments 2>&1)
    Assert-True ($LASTEXITCODE -ne 0) "CheckOnly accepted an invalid endpoint: $($invalidOutput -join ' ')"

    $expectedFiles = @('Start-HLClient.ps1', 'hlclient.exe', 'SDL3.dll', 'steam_api.dll')
    $actualFiles = @(Get-ChildItem -LiteralPath $fixture -File -Recurse | ForEach-Object Name)
    Assert-True ((Compare-Object $expectedFiles $actualFiles | Measure-Object).Count -eq 0) 'CheckOnly created a settings file, log, report, or other unexpected artifact.'

    $settingsPath = Join-Path $launcher 'portable-settings.json'
    $selection = [ordered]@{
        schema = 'hlclient.portable-settings.v1'
        server = '127.0.0.1:27016'
        name = 'Portable Tester'
        half_life_path = $game
        display_index = 1
        window_mode = 'exclusive'
        width = 1920
        height = 1080
        vsync = $false
        msaa = '8'
        hud_scale = '0.75'
        audio_volume = 62
        brightness = -0.10
        gamma = 1.15
        contrast = 1.20
    }
    $savedJson = ConvertTo-Json -InputObject $selection -Compress
    [System.IO.File]::WriteAllText($settingsPath, $savedJson, [System.Text.UTF8Encoding]::new($false))
    $previewArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcherScript,
        '-PreviewArguments', '-HalfLifePath', $game, '-Server', '127.0.0.1:27015')
    $previewOutput = @(& $ps51 @previewArguments 2>&1)
    Assert-True ($LASTEXITCODE -eq 0) "PreviewArguments failed: $($previewOutput -join ' ')"
    $argumentLines = @($previewOutput | Where-Object { [string]$_ -like 'Client arguments:*' })
    Assert-True ($argumentLines.Count -eq 1) 'PreviewArguments did not emit one argument vector.'
    $json = ([string]$argumentLines[0]).Substring('Client arguments: '.Length)
    $vector = [string[]](ConvertFrom-Json -InputObject $json)
    Assert-True ($vector -contains '--no-session-reports') "Portable profile omitted --no-session-reports: $json"
    foreach ($forbidden in @('--net-trace', '--movement-diagnostics', '--session-receipt-root',
                             '--hud-diagnostics', '--external-config')) {
        Assert-True ($vector -cnotcontains $forbidden) "Portable profile included diagnostic argument $forbidden."
    }
    $expectedValues = @{
        '--connect' = '127.0.0.1:27015'
        '--basedir' = $game
        '--steam-api-runtime' = (Join-Path $game 'steam_api.dll')
        '--name' = 'Portable Tester'
        '--display-index' = '1'
        '--window-mode' = 'exclusive'
        '--width' = '1920'
        '--height' = '1080'
        '--vsync' = '0'
        '--msaa' = '8'
        '--hud-scale' = '0.75'
        '--audio-volume' = '62'
        '--brightness' = '-0.10'
        '--gamma' = '1.15'
        '--contrast' = '1.20'
    }
    foreach ($flag in $expectedValues.Keys) {
        Assert-True ((Get-ArgumentValue $vector $flag) -ceq $expectedValues[$flag]) "Wrong child argument for $flag."
    }

    $selection.window_mode = 'invalid'
    [System.IO.File]::WriteAllText($settingsPath, (ConvertTo-Json -InputObject $selection -Compress),
        [System.Text.UTF8Encoding]::new($false))
    $badSettingsOutput = @(& $ps51 @previewArguments 2>&1)
    Assert-True ($LASTEXITCODE -ne 0) "Invalid saved settings were accepted: $($badSettingsOutput -join ' ')"
    $expectedFiles += 'portable-settings.json'
    $actualFiles = @(Get-ChildItem -LiteralPath $fixture -File -Recurse | ForEach-Object Name)
    Assert-True ((Compare-Object $expectedFiles $actualFiles | Measure-Object).Count -eq 0) 'PreviewArguments created a log, report, or unexpected artifact.'
    Write-Host 'PASS portable launcher under Windows PowerShell 5.1: Unicode paths, settings-to-argv, invalid input, and no Steam/game/network launch.'
}
finally {
    if (Test-Path -LiteralPath $fixture) {
        $resolved = [System.IO.Path]::GetFullPath($fixture)
        if (-not $resolved.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a fixture outside the temporary directory: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
