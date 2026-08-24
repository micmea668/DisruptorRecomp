param(
    [switch]$MouseAim,
    [switch]$VerticalLook,
    [switch]$ModernControls,
    [switch]$Widescreen,
    [switch]$GeometryCorrection,
    [switch]$PerspectiveTextures
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Disc = Join-Path $Root 'input/Disruptor (USA).cue'
$Candidates = @(
    (Join-Path $Root 'build/DisruptorRecompiled.exe'),
    (Join-Path $Root 'build/Release/DisruptorRecompiled.exe')
)
$Runtime = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (!$Runtime) { throw "DisruptorRecompiled.exe was not found. Run .\build.ps1 first." }
if (!(Test-Path $Disc)) {
    throw "The full Disruptor BIN/CUE is required. Expected: input/Disruptor (USA).cue"
}

if ($MouseAim -or $VerticalLook -or $ModernControls) {
    if (!(Test-Path (Join-Path $Root 'mouse-aim.ini'))) {
        throw 'mouse-aim.ini is missing.'
    }
    Write-Host 'Mouse camera input enabled: middle-click in gameplay to capture; middle-click or Esc releases.'
}

$RuntimeDirectory = Split-Path -Parent $Runtime
$KeybindPresetName = if ($ModernControls) {
    'keybinds-modern.ini'
} else {
    'keybinds-original.ini'
}
$KeybindPreset = Join-Path $Root $KeybindPresetName
Copy-Item -LiteralPath $KeybindPreset -Destination (Join-Path $RuntimeDirectory 'keybinds.ini') -Force

$GameConfig = if ($Widescreen) { 'game-widescreen.toml' } else { 'game.toml' }
if ($Widescreen) {
    Write-Host 'Experimental native-wide 16:9 enabled; menus and movies remain 4:3.'
}
if ($ModernControls) {
    Write-Host 'Modern controls enabled: WASD, LMB fire, RMB psionic, Space jump, E use.'
}
if ($VerticalLook) {
    Write-Host 'Experimental vertical camera and weapon aim enabled.'
}
if ($GeometryCorrection -or $PerspectiveTextures) {
    Write-Host 'Exact-provenance presentation geometry enabled.'
}
if ($PerspectiveTextures) {
    Write-Host 'Perspective textures enabled for exact world polygons; canonical VRAM and UI remain affine.'
}
Write-Host 'OpenGL dev-menu builds: press the backquote (`) key; Escape closes it.'

$EnvironmentOverrides = @{
    PSX_DISRUPTOR_CONTROL_PROBE = '0'
}
if ($Widescreen) {
    $EnvironmentOverrides['PSX_VIDEO_ASPECT'] = '16:9'
}
if ($ModernControls) {
    $EnvironmentOverrides['PSX_DISRUPTOR_MODERN_CONTROLS'] = '1'
}
if ($VerticalLook) {
    $EnvironmentOverrides['PSX_DISRUPTOR_VERTICAL_LOOK'] = '1'
}
if ($MouseAim -or $ModernControls) {
    $EnvironmentOverrides['PSX_DISRUPTOR_MOUSE_AIM'] = '1'
}
if ($MouseAim -or $VerticalLook -or $ModernControls) {
    $EnvironmentOverrides['PSX_FPS_TELEMETRY'] = '0'
}
if ($GeometryCorrection -or $PerspectiveTextures) {
    $EnvironmentOverrides['PSX_GEOMETRY_CORRECTION'] = '1'
}
if ($PerspectiveTextures) {
    $EnvironmentOverrides['PSX_TEXTURE_CORRECTION'] = '1'
}

$EnvironmentVariableNames = @($EnvironmentOverrides.Keys)
$SavedEnvironment = @{}
foreach ($Name in $EnvironmentVariableNames) {
    $Entry = Get-Item -LiteralPath "Env:$Name" -ErrorAction SilentlyContinue
    $SavedEnvironment[$Name] = @{
        Exists = $null -ne $Entry
        Value = if ($null -ne $Entry) { $Entry.Value } else { $null }
    }
}

$LocationPushed = $false
$RuntimeExitCode = 1
try {
    # Only explicitly requested launch options override persisted settings.
    # These process-environment changes are restored after the child exits.
    foreach ($Name in $EnvironmentVariableNames) {
        [Environment]::SetEnvironmentVariable($Name, $EnvironmentOverrides[$Name], 'Process')
    }

    Push-Location $Root
    $LocationPushed = $true
    & $Runtime --game $GameConfig --disc $Disc
    $RuntimeExitCode = $LASTEXITCODE
}
finally {
    if ($LocationPushed) {
        Pop-Location
    }
    foreach ($Name in $EnvironmentVariableNames) {
        $SavedValue = $SavedEnvironment[$Name]
        if ($SavedValue.Exists) {
            [Environment]::SetEnvironmentVariable($Name, $SavedValue.Value, 'Process')
        }
        else {
            [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
        }
    }
}

exit $RuntimeExitCode
