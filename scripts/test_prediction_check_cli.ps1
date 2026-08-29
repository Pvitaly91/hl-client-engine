param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('raw-authority', 'zero-commands', 'excessive-delay')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$Basedir
)

$ErrorActionPreference = 'Stop'

$arguments = @(
    '--basedir', $Basedir,
    '--game', 'valve',
    '--map', 'maps/test_movement.bsp',
    '--scenario', 'exact-authority'
)

switch ($Mode) {
    'raw-authority' {
        $arguments += @('--raw-authority', '1')
    }
    'zero-commands' {
        $arguments += @('--commands', '0')
    }
    'excessive-delay' {
        $arguments[7] = 'delayed-authority'
        $arguments += @('--authority-delay-commands', '65')
    }
}

$output = & $ToolPath @arguments 2>&1 | Out-String
$exitCode = $LASTEXITCODE
Write-Output $output.TrimEnd()

if ($exitCode -eq 0) {
    throw "Prediction checker accepted forbidden CLI mode '$Mode'."
}
if ($output -notmatch 'Usage: hlclient_prediction_check') {
    throw "Prediction checker rejected '$Mode' without its bounded usage response."
}
if ($output -match '\[prediction\] result=success') {
    throw "Prediction checker published success for forbidden CLI mode '$Mode'."
}
