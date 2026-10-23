param(
    [Parameter(Position = 0)][string]$Server,
    [string]$HalfLifePath,
    [switch]$CheckOnly,
    [switch]$PreviewArguments
)

$ErrorActionPreference = 'Stop'

function Add-UniquePath {
    param([System.Collections.Generic.List[string]]$Paths, [string]$Candidate)
    if ([string]::IsNullOrWhiteSpace($Candidate)) { return }
    try {
        $normalized = [System.IO.Path]::GetFullPath($Candidate.Trim().Trim('"'))
        if (-not [System.IO.Path]::IsPathRooted($normalized)) { return }
        if (-not [System.IO.Directory]::Exists($normalized)) { return }
        foreach ($existing in $Paths) {
            if ([string]::Equals($existing, $normalized, [StringComparison]::OrdinalIgnoreCase)) { return }
        }
        $Paths.Add($normalized)
    } catch { }
}

function Get-SteamRoots {
    $roots = New-Object 'System.Collections.Generic.List[string]'
    foreach ($key in @('HKCU:\Software\Valve\Steam',
                     'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
                     'HKLM:\SOFTWARE\Valve\Steam')) {
        $values = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($null -eq $values) { continue }
        foreach ($field in @('SteamPath', 'InstallPath')) {
            Add-UniquePath $roots ([string]$values.$field)
        }
        if ($values.SteamExe) {
            Add-UniquePath $roots ([System.IO.Path]::GetDirectoryName([string]$values.SteamExe))
        }
    }
    if (${env:ProgramFiles(x86)}) {
        Add-UniquePath $roots (Join-Path ${env:ProgramFiles(x86)} 'Steam')
    }
    if ($env:ProgramFiles) {
        Add-UniquePath $roots (Join-Path $env:ProgramFiles 'Steam')
    }
    return ,$roots
}

function Get-HalfLifeInstallations {
    param([System.Collections.Generic.List[string]]$SteamRoots)
    $libraries = New-Object 'System.Collections.Generic.List[string]'
    foreach ($root in $SteamRoots) {
        Add-UniquePath $libraries $root
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $vdf -PathType Leaf)) { continue }
        try {
            $info = Get-Item -LiteralPath $vdf
            if ($info.Length -gt 1048576) { continue }
            $contents = [System.IO.File]::ReadAllText($vdf)
            foreach ($match in [regex]::Matches($contents, '"path"\s*"([^"\r\n]{1,512})"',
                                                   [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                $path = $match.Groups[1].Value.Replace('\\', '\')
                Add-UniquePath $libraries $path
            }
        } catch { }
    }
    $installs = New-Object 'System.Collections.Generic.List[string]'
    foreach ($library in $libraries) {
        $game = Join-Path $library 'steamapps\common\Half-Life'
        if ((Test-Path -LiteralPath (Join-Path $game 'valve') -PathType Container) -and
            (Test-Path -LiteralPath (Join-Path $game 'steam_api.dll') -PathType Leaf)) {
            Add-UniquePath $installs $game
        }
    }
    return ,$installs
}

function Test-ServerEndpoint {
    param([string]$Endpoint)
    if ($Endpoint -notmatch '^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}):(\d{1,5})$') { return $false }
    foreach ($part in $matches[1..4]) { if ([int]$part -gt 255) { return $false } }
    return [int]$matches[5] -ge 1 -and [int]$matches[5] -le 65535
}

function Test-HalfLifeFolder {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    try {
        $full = [System.IO.Path]::GetFullPath($Path.Trim().Trim('"'))
        return [System.IO.Directory]::Exists((Join-Path $full 'valve')) -and
            [System.IO.File]::Exists((Join-Path $full 'steam_api.dll'))
    } catch { return $false }
}

function Test-PortableSettings {
    param($Value)
    if ($null -eq $Value -or $Value.schema -ne 'hlclient.portable-settings.v1' -or
        -not (Test-ServerEndpoint ([string]$Value.server)) -or
        [string]$Value.name -notmatch '^[\x20-\x7e]{1,31}$' -or
        [string]::IsNullOrWhiteSpace([string]$Value.half_life_path) -or
        ([string]$Value.half_life_path).Length -gt 32768 -or
        $Value.window_mode -notin @('windowed', 'borderless', 'exclusive') -or
        [string]$Value.msaa -notin @('off', '2', '4', '8') -or
        $Value.vsync -isnot [bool]) { return $false }
    try {
        $display = [int]$Value.display_index
        $width = [int]$Value.width
        $height = [int]$Value.height
        $volume = [int]$Value.audio_volume
        $brightness = [double]$Value.brightness
        $gamma = [double]$Value.gamma
        $contrast = [double]$Value.contrast
        if ($display -lt 0 -or $display -gt 31 -or $width -lt 640 -or $width -gt 7680 -or
            $height -lt 480 -or $height -gt 4320 -or
            ([long]$width * [long]$height) -gt (32 * 1024 * 1024) -or
            $volume -lt 0 -or $volume -gt 100 -or
            [double]::IsNaN($brightness) -or [double]::IsInfinity($brightness) -or
            [double]::IsNaN($gamma) -or [double]::IsInfinity($gamma) -or
            [double]::IsNaN($contrast) -or [double]::IsInfinity($contrast) -or
            $brightness -lt -0.5 -or $brightness -gt 0.5 -or $gamma -lt 0.25 -or
            $gamma -gt 4.0 -or $contrast -lt 0 -or $contrast -gt 2.0) { return $false }
        if ([string]$Value.hud_scale -ne 'auto') {
            $scale = [double]::Parse([string]$Value.hud_scale,
                [System.Globalization.CultureInfo]::InvariantCulture)
            if ([double]::IsNaN($scale) -or [double]::IsInfinity($scale) -or
                $scale -lt 0.5 -or $scale -gt 2.0) { return $false }
        }
        return $true
    } catch { return $false }
}

function Read-PortableSettings {
    param([string]$Path, [switch]$Strict)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        $file = Get-Item -LiteralPath $Path
        if ($file.Length -gt 8192) { throw 'Settings file exceeds the size limit.' }
        $value = ConvertFrom-Json -InputObject ([System.IO.File]::ReadAllText($Path))
        if (-not (Test-PortableSettings $value)) { throw 'Saved portable settings are invalid.' }
        return $value
    } catch {
        if ($Strict) { throw }
        return $null
    }
}

function New-LaunchSelection {
    param([string]$Endpoint, [string]$Game, $Saved)
    $value = [pscustomobject]@{
        schema = 'hlclient.portable-settings.v1'
        server = $Endpoint
        name = 'Player'
        half_life_path = $Game
        display_index = 0
        window_mode = 'windowed'
        width = 1280
        height = 720
        vsync = $true
        msaa = '4'
        hud_scale = 'auto'
        audio_volume = 35
        brightness = 0.0
        gamma = 1.0
        contrast = 1.0
    }
    if ($null -ne $Saved) {
        foreach ($field in @('name', 'display_index', 'window_mode', 'width', 'height',
                             'vsync', 'msaa', 'hud_scale', 'audio_volume',
                             'brightness', 'gamma', 'contrast')) {
            $value.$field = $Saved.$field
        }
    }
    return $value
}

function New-ClientArguments {
    param($Selection, [string]$Runtime)
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    return @(
        '--renderer', 'opengl', '--connect', [string]$Selection.server,
        '--stop-after', 'live-visual-control', '--live-input', 'keyboard-mouse',
        '--prediction', 'reference', '--live-session-unlimited', '--no-session-reports',
        '--auth-provider', 'steam', '--steam-api-runtime', $Runtime,
        '--basedir', [string]$Selection.half_life_path, '--game', 'valve',
        '--name', [string]$Selection.name,
        '--display-index', [string]$Selection.display_index,
        '--window-mode', [string]$Selection.window_mode,
        '--width', [string]$Selection.width, '--height', [string]$Selection.height,
        '--vsync', $(if ($Selection.vsync) { '1' } else { '0' }),
        '--msaa', [string]$Selection.msaa, '--hud-scale', [string]$Selection.hud_scale,
        '--audio-volume', [string]$Selection.audio_volume,
        '--brightness', ([double]$Selection.brightness).ToString('0.00', $culture),
        '--gamma', ([double]$Selection.gamma).ToString('0.00', $culture),
        '--contrast', ([double]$Selection.contrast).ToString('0.00', $culture)
    )
}

function Show-PortableLauncher {
    param($Initial, [scriptblock]$InspectForm)
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    if (-not ('PortableDisplayModes' -as [type])) {
        try {
            Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class PortableDisplayModes {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields;
        public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags,
            dmDisplayFrequency, dmICMMethod, dmICMIntent, dmMediaType, dmDitherType,
            dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool EnumDisplaySettings(string deviceName, int modeNumber,
        ref DEVMODE mode);
    public static int[] GetResolutionPairs(string deviceName) {
        var result = new List<int>();
        for (int i = 0; i < 512; ++i) {
            var mode = new DEVMODE();
            mode.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
            if (!EnumDisplaySettings(deviceName, i, ref mode)) break;
            if (mode.dmBitsPerPel < 24 || mode.dmPelsWidth < 640 ||
                mode.dmPelsWidth > 7680 || mode.dmPelsHeight < 480 ||
                mode.dmPelsHeight > 4320) continue;
            result.Add(mode.dmPelsWidth);
            result.Add(mode.dmPelsHeight);
        }
        return result.ToArray();
    }
}
'@
        } catch {
            # A Windows display driver may refuse enumeration. Windowed and
            # borderless still work; never guess an exclusive mode.
        }
    }
    [System.Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'HL Client - portable launcher'
    $form.ClientSize = [System.Drawing.Size]::new(640, 610)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.AutoScaleMode = 'Dpi'

    function Label([string]$caption, [int]$x, [int]$y, [int]$width = 125,
                   [System.Windows.Forms.Control]$parent = $form) {
        $control = New-Object System.Windows.Forms.Label
        $control.Text = $caption
        $control.Location = [System.Drawing.Point]::new($x, $y)
        $control.Size = [System.Drawing.Size]::new($width, 24)
        [void]$parent.Controls.Add($control)
    }
    function TextBox([string]$value, [int]$x, [int]$y, [int]$width) {
        $control = New-Object System.Windows.Forms.TextBox
        $control.Location = [System.Drawing.Point]::new($x, $y)
        $control.Size = [System.Drawing.Size]::new($width, 24)
        $control.Text = $value
        [void]$form.Controls.Add($control)
        return $control
    }
    function Combo([int]$x, [int]$y, [int]$width,
                   [System.Windows.Forms.Control]$parent = $form) {
        $control = New-Object System.Windows.Forms.ComboBox
        $control.DropDownStyle = 'DropDownList'
        $control.Location = [System.Drawing.Point]::new($x, $y)
        $control.Size = [System.Drawing.Size]::new($width, 24)
        [void]$parent.Controls.Add($control)
        return $control
    }
    function Number([decimal]$min, [decimal]$max, [decimal]$step,
                    [int]$digits, [int]$x, [int]$y, [int]$width,
                    [System.Windows.Forms.Control]$parent = $form) {
        $control = New-Object System.Windows.Forms.NumericUpDown
        $control.Minimum = $min
        $control.Maximum = $max
        $control.Increment = $step
        $control.DecimalPlaces = $digits
        $control.Location = [System.Drawing.Point]::new($x, $y)
        $control.Size = [System.Drawing.Size]::new($width, 24)
        [void]$parent.Controls.Add($control)
        return $control
    }

    Label 'Server (IP:port)' 16 18
    $serverBox = TextBox ([string]$Initial.server) 155 16 465
    Label 'Player name' 16 50
    $nameBox = TextBox ([string]$Initial.name) 155 48 465
    $nameBox.MaxLength = 31
    Label 'Steam Half-Life' 16 82
    $gameBox = TextBox ([string]$Initial.half_life_path) 155 80 365
    $browse = New-Object System.Windows.Forms.Button
    $browse.Text = 'Browse...'
    $browse.Location = [System.Drawing.Point]::new(527, 78)
    $browse.Size = [System.Drawing.Size]::new(93, 27)
    [void]$form.Controls.Add($browse)
    $browse.Add_Click({
        $picker = New-Object System.Windows.Forms.FolderBrowserDialog
        $picker.Description = 'Select the Steam Half-Life folder containing valve and steam_api.dll'
        if ([System.IO.Directory]::Exists($gameBox.Text)) { $picker.SelectedPath = $gameBox.Text }
        if ($picker.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
            $gameBox.Text = $picker.SelectedPath
        }
        $picker.Dispose()
    })

    $videoGroup = New-Object System.Windows.Forms.GroupBox
    $videoGroup.Name = 'DisplaySettings'
    $videoGroup.Text = 'Display'
    $videoGroup.Location = [System.Drawing.Point]::new(16, 118)
    $videoGroup.Size = [System.Drawing.Size]::new(604, 210)
    [void]$form.Controls.Add($videoGroup)
    # Settings must be children of their group, with group-relative bounds.
    # Sibling controls on the form are covered by the GroupBox background.
    Label 'Monitor' 15 27 110 $videoGroup
    $displayBox = Combo 139 24 445 $videoGroup
    $displayBox.Name = 'Monitor'
    $screens = @([System.Windows.Forms.Screen]::AllScreens)
    for ($i = 0; $i -lt $screens.Count; $i++) {
        $screen = $screens[$i]
        $primary = if ($screen.Primary) { ' (primary)' } else { '' }
        [void]$displayBox.Items.Add(('{0}: {1} x {2}{3}' -f
            ($i + 1), $screen.Bounds.Width, $screen.Bounds.Height, $primary))
    }
    if ($screens.Count -eq 0) { throw 'No usable monitor was found.' }
    $displayIndex = [int]$Initial.display_index
    if ($displayIndex -ge $screens.Count) { $displayIndex = 0 }
    $displayBox.SelectedIndex = $displayIndex
    Label 'Screen mode' 15 59 110 $videoGroup
    $modeBox = Combo 139 56 445 $videoGroup
    $modeBox.Name = 'ScreenMode'
    foreach ($mode in @('Windowed', 'Fullscreen (borderless)', 'Fullscreen (exclusive)')) {
        [void]$modeBox.Items.Add($mode)
    }
    $modeBox.SelectedIndex = switch ([string]$Initial.window_mode) {
        'borderless' { 1 } 'exclusive' { 2 } default { 0 }
    }
    Label 'Resolution' 15 91 110 $videoGroup
    $resolutionBox = Combo 139 88 445 $videoGroup
    $resolutionBox.Name = 'Resolution'
    $vsyncBox = New-Object System.Windows.Forms.CheckBox
    $vsyncBox.Text = 'VSync'
    $vsyncBox.Name = 'VSync'
    $vsyncBox.Checked = [bool]$Initial.vsync
    $vsyncBox.Location = [System.Drawing.Point]::new(139, 121)
    $vsyncBox.Size = [System.Drawing.Size]::new(110, 25)
    [void]$videoGroup.Controls.Add($vsyncBox)
    Label 'Antialiasing' 15 156 110 $videoGroup
    $msaaBox = Combo 139 153 150 $videoGroup
    $msaaBox.Name = 'Antialiasing'
    foreach ($msaa in @('Off', '2x', '4x', '8x')) { [void]$msaaBox.Items.Add($msaa) }
    $msaaBox.SelectedIndex = switch ([string]$Initial.msaa) {
        '2' { 1 } '4' { 2 } '8' { 3 } default { 0 }
    }
    $modeHint = New-Object System.Windows.Forms.Label
    $modeHint.Location = [System.Drawing.Point]::new(299, 153)
    $modeHint.Size = [System.Drawing.Size]::new(285, 48)
    [void]$videoGroup.Controls.Add($modeHint)
    $refreshResolution = {
        if ($displayBox.SelectedIndex -lt 0 -or $modeBox.SelectedIndex -lt 0) { return }
        $screen = $screens[$displayBox.SelectedIndex]
        $desktopWidth = $screen.Bounds.Width
        $desktopHeight = $screen.Bounds.Height
        $borderless = $modeBox.SelectedIndex -eq 1
        $resolutionBox.Items.Clear()
        $wanted = ('{0} x {1}' -f [int]$Initial.width, [int]$Initial.height)
        if ($borderless) {
            [void]$resolutionBox.Items.Add(('{0} x {1}' -f $desktopWidth, $desktopHeight))
            $resolutionBox.SelectedIndex = 0
            $resolutionBox.Enabled = $false
            $modeHint.Text = 'Borderless uses the monitor desktop resolution.'
        } else {
            if ($modeBox.SelectedIndex -eq 2) {
                $sizes = @()
                if ('PortableDisplayModes' -as [type]) {
                    $pairs = [PortableDisplayModes]::GetResolutionPairs($screen.DeviceName)
                    for ($pair = 0; $pair + 1 -lt $pairs.Length; $pair += 2) {
                        $sizes += ,@([int]$pairs[$pair], [int]$pairs[$pair + 1])
                    }
                }
                if ($sizes.Count -eq 0) {
                    $modeBox.SelectedIndex = 1
                    $modeHint.Text = 'Exclusive modes unavailable; using borderless.'
                    return
                }
            } else {
                $sizes = @(
                    @(640, 480), @(800, 600), @(1024, 768), @(1280, 720),
                    @(1280, 800), @(1366, 768), @(1600, 900), @(1920, 1080),
                    @(2560, 1440), @(3840, 2160),
                    @($desktopWidth, $desktopHeight),
                    @([int]$Initial.width, [int]$Initial.height)
                )
            }
            $seen = @{}
            foreach ($size in $sizes) {
                $width = [int]$size[0]; $height = [int]$size[1]
                if ($width -lt 640 -or $height -lt 480 -or
                    ($modeBox.SelectedIndex -eq 0 -and
                     ($width -gt $desktopWidth -or $height -gt $desktopHeight)) -or
                    $width -gt 7680 -or $height -gt 4320) { continue }
                $label = '{0} x {1}' -f $width, $height
                if (-not $seen.ContainsKey($label)) {
                    [void]$resolutionBox.Items.Add($label)
                    $seen[$label] = $true
                }
            }
            if ($resolutionBox.Items.Count -eq 0) {
                if ($modeBox.SelectedIndex -eq 2) {
                    $modeBox.SelectedIndex = 1
                    $modeHint.Text = 'Exclusive modes unavailable; using borderless.'
                    return
                }
                [void]$resolutionBox.Items.Add(('{0} x {1}' -f $desktopWidth, $desktopHeight))
            }
            $selected = $resolutionBox.Items.IndexOf($wanted)
            $resolutionBox.SelectedIndex = if ($selected -ge 0) { $selected } else { 0 }
            $resolutionBox.Enabled = $true
            $modeHint.Text = if ($modeBox.SelectedIndex -eq 2) {
                'Exclusive requires a display-supported mode; fallback is windowed.'
            } else { 'Windowed size; desktop resolution is unchanged.' }
        }
    }
    $displayBox.Add_SelectedIndexChanged($refreshResolution)
    $modeBox.Add_SelectedIndexChanged($refreshResolution)
    & $refreshResolution

    $settingsGroup = New-Object System.Windows.Forms.GroupBox
    $settingsGroup.Name = 'ImageHudSoundSettings'
    $settingsGroup.Text = 'Image, HUD and sound'
    $settingsGroup.Location = [System.Drawing.Point]::new(16, 338)
    $settingsGroup.Size = [System.Drawing.Size]::new(604, 200)
    [void]$form.Controls.Add($settingsGroup)
    Label 'Brightness' 15 25 110 $settingsGroup
    $brightnessBox = Number ([decimal]-0.50) 0.50 0.05 2 139 22 100 $settingsGroup
    $brightnessBox.Name = 'Brightness'
    $brightnessBox.Value = [decimal]$Initial.brightness
    Label 'Gamma' 254 25 80 $settingsGroup
    $gammaBox = Number 0.25 4.00 0.05 2 344 22 100 $settingsGroup
    $gammaBox.Name = 'Gamma'
    $gammaBox.Value = [decimal]$Initial.gamma
    Label 'Contrast' 15 60 110 $settingsGroup
    $contrastBox = Number 0.00 2.00 0.05 2 139 57 100 $settingsGroup
    $contrastBox.Name = 'Contrast'
    $contrastBox.Value = [decimal]$Initial.contrast
    Label 'HUD scale' 15 95 110 $settingsGroup
    $hudAuto = New-Object System.Windows.Forms.CheckBox
    $hudAuto.Text = 'Auto'
    $hudAuto.Name = 'HudAuto'
    $hudAuto.Location = [System.Drawing.Point]::new(139, 92)
    $hudAuto.Size = [System.Drawing.Size]::new(85, 24)
    [void]$settingsGroup.Controls.Add($hudAuto)
    $hudBox = Number 50 200 5 0 239 92 100 $settingsGroup
    $hudBox.Name = 'HudScale'
    $hudAuto.Checked = [string]$Initial.hud_scale -eq 'auto'
    if (-not $hudAuto.Checked) {
        $hudBox.Value = [decimal]([double]::Parse(
            [string]$Initial.hud_scale,
            [System.Globalization.CultureInfo]::InvariantCulture) * 100)
    } else { $hudBox.Value = 100 }
    $hudBox.Enabled = -not $hudAuto.Checked
    $hudAuto.Add_CheckedChanged({ $hudBox.Enabled = -not $hudAuto.Checked })
    Label 'Volume (%)' 15 130 110 $settingsGroup
    $volumeBox = Number 0 100 5 0 139 127 100 $settingsGroup
    $volumeBox.Name = 'Volume'
    $volumeBox.Value = [decimal]$Initial.audio_volume

    $launch = New-Object System.Windows.Forms.Button
    $launch.Text = 'Launch'
    $launch.Location = [System.Drawing.Point]::new(426, 552)
    $launch.Size = [System.Drawing.Size]::new(94, 31)
    [void]$form.Controls.Add($launch)
    $form.AcceptButton = $launch
    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = 'Cancel'
    $cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $cancel.Location = [System.Drawing.Point]::new(526, 552)
    $cancel.Size = [System.Drawing.Size]::new(94, 31)
    [void]$form.Controls.Add($cancel)
    $form.CancelButton = $cancel
    $note = New-Object System.Windows.Forms.Label
    $note.Text = 'Requires Steam Half-Life. Startup logs: logs folder (no full session reports).'
    $note.Location = [System.Drawing.Point]::new(16, 586)
    $note.Size = [System.Drawing.Size]::new(605, 20)
    [void]$form.Controls.Add($note)

    $launch.Add_Click({
        try {
            $endpoint = $serverBox.Text.Trim()
            if (-not (Test-ServerEndpoint $endpoint)) { throw 'Enter a valid IPv4:port server address.' }
            $player = $nameBox.Text.Trim()
            if ($player -notmatch '^[\x20-\x7e]{1,31}$') {
                throw 'Player name must contain 1-31 printable ASCII characters.'
            }
            if (-not (Test-HalfLifeFolder $gameBox.Text)) {
                throw 'Choose the Steam Half-Life folder containing valve and steam_api.dll.'
            }
            $resolutionMatch = [regex]::Match(
                [string]$resolutionBox.SelectedItem, '^(\d+) x (\d+)$')
            if (-not $resolutionMatch.Success) {
                throw 'Select a valid resolution.'
            }
            $mode = @('windowed', 'borderless', 'exclusive')[$modeBox.SelectedIndex]
            $scale = if ($hudAuto.Checked) { 'auto' } else {
                ([double]$hudBox.Value / 100).ToString('0.00',
                    [System.Globalization.CultureInfo]::InvariantCulture)
            }
            $selected = [pscustomobject]@{
                schema = 'hlclient.portable-settings.v1'
                server = $endpoint
                name = $player
                half_life_path = [System.IO.Path]::GetFullPath($gameBox.Text.Trim().Trim('"'))
                display_index = $displayBox.SelectedIndex
                window_mode = $mode
                width = [int]$resolutionMatch.Groups[1].Value
                height = [int]$resolutionMatch.Groups[2].Value
                vsync = $vsyncBox.Checked
                msaa = @('off', '2', '4', '8')[$msaaBox.SelectedIndex]
                hud_scale = $scale
                audio_volume = [int]$volumeBox.Value
                brightness = [double]$brightnessBox.Value
                gamma = [double]$gammaBox.Value
                contrast = [double]$contrastBox.Value
            }
            if (-not (Test-PortableSettings $selected)) { throw 'Launch settings are outside the supported range.' }
            $form.Tag = $selected
            $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
            $form.Close()
        } catch {
            [void][System.Windows.Forms.MessageBox]::Show(
                $form, $_.Exception.Message, 'HL Client - invalid settings',
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Warning)
        }
    })
    if ($InspectForm) {
        # Offline UI verification uses this exact form without starting Steam
        # or writing user settings. Normal launches always use ShowDialog.
        try { & $InspectForm $form } finally { $form.Dispose() }
        return $null
    }
    $dialogResult = $form.ShowDialog()
    $selection = $form.Tag
    $form.Dispose()
    if ($dialogResult -eq [System.Windows.Forms.DialogResult]::OK) { return $selection }
    return $null
}

try {
    if (-not $CheckOnly -and -not $PreviewArguments) {
        . (Join-Path $PSScriptRoot 'Portable-StartupLogging.ps1')
        Initialize-PortableLog -PackageRoot $PSScriptRoot
        Write-Host ('Startup logs: {0}' -f $script:PortableLogDirectory)
    }
    $client = Join-Path $PSScriptRoot 'hlclient.exe'
    if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
        throw 'hlclient.exe is missing from this portable folder.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'SDL3.dll') -PathType Leaf)) {
        throw 'SDL3.dll is missing from this portable folder.'
    }

    $steamRoots = Get-SteamRoots
    $installs = Get-HalfLifeInstallations $steamRoots
    $settingsPath = Join-Path $PSScriptRoot 'portable-settings.json'
    $saved = Read-PortableSettings $settingsPath -Strict:($CheckOnly -or $PreviewArguments)
    $game = ''
    if ($HalfLifePath) {
        $game = [System.IO.Path]::GetFullPath($HalfLifePath.Trim().Trim('"'))
    } elseif ($null -ne $saved -and (Test-HalfLifeFolder ([string]$saved.half_life_path))) {
        $game = [string]$saved.half_life_path
    } elseif ($installs.Count -gt 0) {
        $game = $installs[0]
    }

    if ([string]::IsNullOrWhiteSpace($Server)) {
        if ($null -ne $saved) { $Server = [string]$saved.server }
    }
    if ([string]::IsNullOrWhiteSpace($Server)) {
        $serverFile = Join-Path $PSScriptRoot 'server.txt'
        if (Test-Path -LiteralPath $serverFile -PathType Leaf) {
            $Server = [string](Get-Content -LiteralPath $serverFile -TotalCount 1)
        }
    }
    $selection = New-LaunchSelection $Server $game $saved
    if (-not $CheckOnly -and -not $PreviewArguments) {
        $selection = Show-PortableLauncher $selection
        if ($null -eq $selection) { Write-PortableLog 'Launch cancelled by user.'; exit 0 }
        try {
            $json = ConvertTo-Json -InputObject $selection -Compress -Depth 3
            [System.IO.File]::WriteAllText($settingsPath, $json,
                ([System.Text.UTF8Encoding]::new($false)))
        } catch {
            [void][System.Windows.Forms.MessageBox]::Show(
                'Settings could not be saved in this folder. The game will still launch.',
                'HL Client - settings', [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Information)
        }
    } else {
        if (-not (Test-HalfLifeFolder ([string]$selection.half_life_path))) {
            throw 'Steam Half-Life folder was not found. Use -HalfLifePath with the full installed game path.'
        }
        if (-not (Test-ServerEndpoint ([string]$selection.server))) {
            throw 'Invalid server address. Edit server.txt or pass IPv4:port to Start-HLClient.cmd.'
        }
    }
    $game = [string]$selection.half_life_path
    $runtime = Join-Path $game 'steam_api.dll'
    $arguments = New-ClientArguments $selection $runtime

    Write-Host ('Half-Life: {0}' -f $game)
    Write-Host ('Server:    {0}' -f $selection.server)
    Write-Host ('Video:     display {0}, {1}, {2} x {3}, VSync={4}, MSAA={5}' -f
        $selection.display_index, $selection.window_mode, $selection.width,
        $selection.height, $selection.vsync, $selection.msaa)
    if ($PreviewArguments) {
        Write-Host ('Client arguments: {0}' -f (ConvertTo-Json -InputObject $arguments -Compress))
        exit 0
    }
    if ($CheckOnly) {
        Write-Host 'Portable configuration is ready; the game was not started.'
        exit 0
    }

    Write-PortableLog ('Half-Life path: {0}' -f $game)
    Write-PortableLog ('Server: {0}; display={1}; mode={2}; size={3}x{4}; vsync={5}; MSAA={6}' -f
        $selection.server, $selection.display_index, $selection.window_mode,
        $selection.width, $selection.height, $selection.vsync, $selection.msaa)
    try {
        $windows = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
        Write-PortableLog ('Windows registry: product={0}; build={1}.{2}; release={3}' -f
            $windows.ProductName, $windows.CurrentBuildNumber, $windows.UBR, $windows.DisplayVersion)
    } catch { Write-PortableLog 'Windows build metadata unavailable.' }
    try {
        $adapters = @(Get-CimInstance -ClassName Win32_VideoController -OperationTimeoutSec 3 -ErrorAction Stop | Select-Object -First 8)
        foreach ($adapter in $adapters) {
            Write-PortableLog ('Display adapter: {0}; driver={1}' -f $adapter.Name, $adapter.DriverVersion)
        }
    } catch { Write-PortableLog 'Display adapter metadata unavailable; startup capture continues.' }
    foreach ($binary in @($client, (Join-Path $PSScriptRoot 'SDL3.dll'),
                         (Join-Path $PSScriptRoot 'msvcp140.dll'),
                         (Join-Path $PSScriptRoot 'msvcp140_atomic_wait.dll'),
                         (Join-Path $PSScriptRoot 'msvcp140_2.dll'),
                         (Join-Path $PSScriptRoot 'vcruntime140.dll'), $runtime)) {
        if (Test-Path -LiteralPath $binary -PathType Leaf) {
            $item = Get-Item -LiteralPath $binary
            Write-PortableLog ('Binary {0}; bytes={1}; version={2}; SHA256={3}' -f
                $item.Name, $item.Length, $item.VersionInfo.FileVersion,
                (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash)
        } else { Write-PortableLog ('Missing binary: {0}' -f [System.IO.Path]::GetFileName($binary)) }
    }
    Write-PortableLog ('Steam running: {0}' -f [bool](Get-Process -Name steam -ErrorAction SilentlyContinue))

    if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
        $steamExe = $null
        foreach ($root in $steamRoots) {
            $candidate = Join-Path $root 'steam.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $steamExe = $candidate
                break
            }
        }
        if ($null -eq $steamExe) {
            throw 'Steam is not running. Sign in to Steam and run this launcher again.'
        }
        Write-Host 'Starting Steam. Sign in if prompted...'
        Start-Process -FilePath $steamExe -ArgumentList '-silent' -WindowStyle Hidden | Out-Null
        $started = $false
        for ($i = 0; $i -lt 20; $i++) {
            Start-Sleep -Seconds 1
            if (Get-Process -Name steam -ErrorAction SilentlyContinue) {
                $started = $true
                break
            }
        }
        if (-not $started) { throw 'Steam did not start. Start Steam manually and retry.' }
        [void][System.Windows.Forms.MessageBox]::Show(
            'Finish signing in to Steam, then click OK to launch HL Client.',
            'HL Client - Steam sign-in', [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Information)
    }

    # Keep heavy session reports disabled; bounded sanitized output is captured
    # by the launcher without changing the client binary or game settings.
    $env:SteamAppId = '70'
    $env:SteamGameId = '70'
    Write-Host 'Starting HL Client...'
    $code = Invoke-PortableClient -Executable $client -Arguments $arguments -WorkingDirectory $PSScriptRoot
    Write-Host ('Startup logs saved: {0}' -f $script:PortableLogDirectory)
    if ($code -ne 0) { throw ('HL Client exited with code {0}.' -f $code) }
    exit 0
} catch {
    $failureMessage = $_.Exception.GetBaseException().Message
    if (Get-Command Write-PortableLog -ErrorAction SilentlyContinue) {
        Write-PortableLog ('Launcher error: ' + $failureMessage)
        if ($script:PortableLogDirectory) {
            $failureMessage += "`r`n`r`nCopy this log folder:`r`n" + $script:PortableLogDirectory
        }
    }
    Write-Host ('ERROR: {0}' -f $failureMessage) -ForegroundColor Red
    if (-not $CheckOnly -and -not $PreviewArguments) {
        try {
            Add-Type -AssemblyName System.Windows.Forms
            [void][System.Windows.Forms.MessageBox]::Show(
                $failureMessage, 'HL Client - error',
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Error)
        } catch { }
    }
    exit 1
}
