[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('forbidden-option', 'sprite-smoke')]
    [string]$Mode,

    [string]$ForbiddenOption
)

$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'hlclient-goldsrc-asset-check-cli-' + [Guid]::NewGuid().ToString('N'))
$spriteDirectory = Join-Path $testRoot 'valve\sprites'
$spritePath = Join-Path $spriteDirectory 'cli_test.spr'

function Write-U8 {
    param([IO.BinaryWriter]$Writer, [byte]$Value)
    $Writer.Write($Value)
}

function Write-I32Le {
    param([IO.BinaryWriter]$Writer, [int]$Value)
    $Writer.Write([int32]$Value)
}

function Write-SyntheticSprite {
    param([string]$Path)

    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        foreach ($value in [byte[]](0x49, 0x44, 0x53, 0x50)) {
            Write-U8 -Writer $writer -Value $value
        }
        Write-I32Le -Writer $writer -Value 2
        Write-I32Le -Writer $writer -Value 0
        Write-I32Le -Writer $writer -Value 0
        $writer.Write([single]1.0)
        Write-I32Le -Writer $writer -Value 1
        Write-I32Le -Writer $writer -Value 1
        Write-I32Le -Writer $writer -Value 1
        $writer.Write([single]0.0)
        Write-I32Le -Writer $writer -Value 0
        $writer.Write([uint16]256)
        for ($index = 0; $index -lt 256; ++$index) {
            Write-U8 -Writer $writer -Value ([byte]$index)
            Write-U8 -Writer $writer -Value ([byte](255 - $index))
            Write-U8 -Writer $writer -Value ([byte]($index -band 0x7f))
        }
        Write-I32Le -Writer $writer -Value 0
        Write-I32Le -Writer $writer -Value 0
        Write-I32Le -Writer $writer -Value 0
        Write-I32Le -Writer $writer -Value 1
        Write-I32Le -Writer $writer -Value 1
        Write-U8 -Writer $writer -Value 7
        $writer.Flush()
        [IO.File]::WriteAllBytes($Path, $stream.ToArray())
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Invoke-Checker {
    param([string[]]$Arguments)

    $lines = @(& $ToolPath @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    return @{
        ExitCode = $LASTEXITCODE
        Lines = $lines
    }
}

try {
    New-Item -ItemType Directory -Path $spriteDirectory -Force | Out-Null
    Write-SyntheticSprite -Path $spritePath

    $arguments = @(
        '--basedir', $testRoot,
        '--game', 'valve',
        '--asset', 'sprites/cli_test.spr',
        '--kind', 'sprite'
    )

    if ($Mode -eq 'forbidden-option') {
        if ([string]::IsNullOrWhiteSpace($ForbiddenOption)) {
            throw 'Forbidden-option mode requires -ForbiddenOption.'
        }
        $arguments += @('--' + $ForbiddenOption, 'rejected')
        $result = Invoke-Checker -Arguments $arguments
        if ($result.ExitCode -ne 2) {
            throw "Forbidden option '$ForbiddenOption' returned exit $($result.ExitCode), expected parser-rejection exit 2."
        }
        if (-not ($result.Lines -match '^Usage: hlclient_goldsrc_asset_check ')) {
            throw "Forbidden option '$ForbiddenOption' did not produce the usage-only parser rejection."
        }
    }
    else {
        $result = Invoke-Checker -Arguments $arguments
        if ($result.ExitCode -ne 0) {
            throw "Positive sprite smoke returned exit $($result.ExitCode): $($result.Lines -join ' | ')"
        }
        $requiredLines = @(
            '[sprite] importer=goldsrc-sprite-v2',
            '[sprite] sources=1',
            '[sprite] version=2',
            '[sprite] orientation=view_parallel_upright',
            '[sprite] format=normal',
            '[sprite] top-level-entries=1',
            '[sprite] entries=1',
            '[sprite] flattened-frames=1',
            '[sprite] frames=1',
            '[sprite] groups=0',
            '[sprite] indexed-bytes=1',
            '[sprite] result=complete'
        )
        foreach ($required in $requiredLines) {
            if ($result.Lines -notcontains $required) {
                throw "Positive sprite smoke omitted required line '$required'."
            }
        }
    }

    $joinedOutput = $result.Lines -join "`n"
    if ($joinedOutput.Contains($testRoot) -or
        $joinedOutput.Contains($spritePath) -or
        $joinedOutput.Contains('cli_test.spr')) {
        throw 'Checker output disclosed a native or virtual test path.'
    }
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedRoot = [IO.Path]::GetFullPath($testRoot).TrimEnd('\')
        $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
        $leaf = [IO.Path]::GetFileName($resolvedRoot)
        $parent = [IO.Path]::GetDirectoryName($resolvedRoot).TrimEnd('\')
        if ($parent -ne $resolvedTemp -or
            $leaf -notmatch '^hlclient-goldsrc-asset-check-cli-[0-9a-f]{32}$') {
            throw "Refusing to remove unexpected test path '$resolvedRoot'."
        }
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
