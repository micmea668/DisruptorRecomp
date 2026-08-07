param(
    [switch]$MouseAim,
    [switch]$ModernControls,
    [switch]$Widescreen
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

$env:PSX_DISRUPTOR_CONTROL_PROBE = '0'
$env:PSX_DISRUPTOR_MODERN_CONTROLS = if ($ModernControls) { '1' } else { '0' }
if ($MouseAim -or $ModernControls) {
    if (!(Test-Path (Join-Path $Root 'mouse-aim.ini'))) {
        throw 'mouse-aim.ini is missing.'
    }
    $env:PSX_DISRUPTOR_MOUSE_AIM = '1'
    $env:PSX_FPS_TELEMETRY = '0'
    Write-Host 'Mouse aim enabled: middle-click in gameplay to capture; middle-click or Esc releases.'
}
else {
    $env:PSX_DISRUPTOR_MOUSE_AIM = '0'
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

Push-Location $Root
try {
    & $Runtime --game $GameConfig --disc $Disc
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
