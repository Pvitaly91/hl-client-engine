param([string]$ScreenshotPath)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $repository 'portable\Start-HLClient.ps1'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
# Load only function definitions, never the launcher's Steam/game entry point.
foreach ($statement in $ast.EndBlock.Statements) {
    if ($statement -is [System.Management.Automation.Language.FunctionDefinitionAst]) {
        . ([scriptblock]::Create($statement.Extent.Text))
    }
}

function Assert-Layout([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$initial = New-LaunchSelection '127.0.0.1:27015' 'C:\Steam\Half-Life' $null
Show-PortableLauncher $initial -InspectForm {
    param($window)
    # Exercise actual WinForms handles, paint and hit testing without stealing
    # focus or opening a visible test window on the user's desktop.
    $window.ShowInTaskbar = $false
    $window.StartPosition = 'Manual'
    $window.Location = [System.Drawing.Point]::new(-32000, -32000)
    $window.Show()
    [System.Windows.Forms.Application]::DoEvents()
    $expected = @{
        DisplaySettings = @('Monitor', 'ScreenMode', 'Resolution', 'VSync', 'Antialiasing')
        ImageHudSoundSettings = @('Brightness', 'Gamma', 'Contrast', 'HudAuto', 'HudScale', 'Volume')
    }
    foreach ($groupName in $expected.Keys) {
        $group = $window.Controls[$groupName]
        Assert-Layout ($null -ne $group) "Missing group: $groupName"
        foreach ($name in $expected[$groupName]) {
            $control = $group.Controls[$name]
            Assert-Layout ($null -ne $control) "Setting is not inside its group: $name"
            Assert-Layout ($control.Visible) "Setting is not visible: $name"
            Assert-Layout ($group.ClientRectangle.Contains($control.Bounds)) "Setting is clipped: $name"
            $center = [System.Drawing.Point]::new($control.Left + [int]($control.Width / 2),
                $control.Top + [int]($control.Height / 2))
            Assert-Layout ($group.GetChildAtPoint($center) -eq $control) "Setting is covered: $name"
            $formPoint = [System.Drawing.Point]::new($group.Left + $center.X, $group.Top + $center.Y)
            Assert-Layout ($window.GetChildAtPoint($formPoint) -eq $group) "Group is covered: $groupName"
        }
    }
    $video = $window.Controls['DisplaySettings']
    $mode = $video.Controls['ScreenMode']
    $resolution = $video.Controls['Resolution']
    Assert-Layout ($mode.Items.Count -eq 3) 'Screen mode choices are missing.'
    $mode.SelectedIndex = 1
    [System.Windows.Forms.Application]::DoEvents()
    Assert-Layout (-not $resolution.Enabled -and $resolution.Items.Count -eq 1) 'Borderless resolution is not fixed to desktop.'
    $mode.SelectedIndex = 2
    [System.Windows.Forms.Application]::DoEvents()
    Assert-Layout ($resolution.SelectedIndex -ge 0) 'Fullscreen has no selected resolution.'
    $mode.SelectedIndex = 0
    Assert-Layout ($resolution.Enabled -and $resolution.Items.Count -gt 0) 'Windowed resolutions are unavailable.'
    $settings = $window.Controls['ImageHudSoundSettings']
    $settings.Controls['HudAuto'].Checked = $false
    Assert-Layout ($settings.Controls['HudScale'].Enabled) 'Manual HUD control is inaccessible.'
    $settings.Controls['HudAuto'].Checked = $true
    $window.Refresh()
    [System.Windows.Forms.Application]::DoEvents()
    if ($ScreenshotPath) {
        $bitmap = [System.Drawing.Bitmap]::new($window.Width, $window.Height)
        try {
            $window.DrawToBitmap($bitmap, [System.Drawing.Rectangle]::new(0, 0, $window.Width, $window.Height))
            $bitmap.Save([System.IO.Path]::GetFullPath($ScreenshotPath), [System.Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose() }
    }
    $window.Close()
}
Write-Output 'PASS real WinForms layout: 11 settings visible, unclipped and hit-testable; mode and HUD controls respond.'
