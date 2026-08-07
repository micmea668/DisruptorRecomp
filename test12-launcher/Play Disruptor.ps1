$ErrorActionPreference = 'Stop'

try {
    $Root = Split-Path -Parent $MyInvocation.MyCommand.Path
    $Runtime = Join-Path $Root 'DisruptorRecompiled.exe'
    $DefaultCue = Join-Path $Root 'input\Disruptor (USA).cue'
    $DefaultBin = Join-Path $Root 'input\Disruptor (USA).bin'
    $MouseAimIni = Join-Path $Root 'mouse-aim.ini'
    $MouseAimLog = Join-Path $Root 'disruptor-mouse-aim.log'

    $ExperimentMode = [string]$env:DISRUPTOR_EXPERIMENT_MODE
    if (@('exact-camera', 'exact-geometry', 'exact-camera-adaptive', 'baseline') -notcontains $ExperimentMode) {
        $ExperimentMode = 'exact-geometry'
    }
    $GeometryCorrection = $ExperimentMode -ne 'baseline'
    $HighPrecisionCamera = @('exact-camera', 'exact-camera-adaptive') -contains $ExperimentMode
    $InterpolationMode = if ($ExperimentMode -eq 'exact-camera-adaptive') { 'adaptive' } else { 'off' }
    $IsWide = $true
    $RunKey = $ExperimentMode
    $ModeTitle = switch ($ExperimentMode) {
        'exact-camera'          { 'exact-provenance geometry + fractional mouse yaw; no frame blending' }
        'exact-geometry'        { 'exact-provenance geometry only; stock byte yaw and no frame blending' }
        'exact-camera-adaptive' { 'exact-provenance geometry + fractional mouse yaw + adaptive presentation' }
        default                 { 'Test 9-style baseline; correction and frame blending disabled' }
    }

    if (!(Test-Path -LiteralPath $Runtime)) {
        throw 'DisruptorRecompiled.exe is missing from this folder.'
    }

    $Disc = $null
    if ($args.Count -gt 0 -and $args[0]) {
        $Disc = $args[0]
    }
    elseif ((Test-Path -LiteralPath $DefaultCue) -and
            (Test-Path -LiteralPath $DefaultBin)) {
        $Disc = $DefaultCue
    }
    else {
        Add-Type -AssemblyName System.Windows.Forms
        $Picker = New-Object System.Windows.Forms.OpenFileDialog
        $Picker.Title = 'Select your Disruptor (USA) CUE file'
        $Picker.Filter = 'PlayStation cue sheet (*.cue)|*.cue|Raw disc track (*.bin)|*.bin|All files (*.*)|*.*'
        $Picker.CheckFileExists = $true
        if ($Picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            throw 'No disc file was selected.'
        }
        $Disc = $Picker.FileName
    }

    $Disc = (Resolve-Path -LiteralPath $Disc).Path
    New-Item -ItemType Directory -Force -Path (Join-Path $Root 'saves') | Out-Null

    $ConfigName = if ($IsWide) { 'game-widescreen.toml' } else { 'game-4-3.toml' }
    $GameConfig = Join-Path $Root $ConfigName
    $KeybindPreset = Join-Path $Root 'keybinds-modern.ini'
    if (!(Test-Path -LiteralPath $GameConfig)) {
        throw "$ConfigName is missing from this folder."
    }
    if (!(Test-Path -LiteralPath $KeybindPreset)) {
        throw 'keybinds-modern.ini is missing from this folder.'
    }
    if (!(Test-Path -LiteralPath $MouseAimIni)) {
        throw 'mouse-aim.ini is missing from this folder.'
    }
    Copy-Item -LiteralPath $KeybindPreset -Destination (Join-Path $Root 'keybinds.ini') -Force

    $env:PSX_DISRUPTOR_CONTROL_PROBE = '0'
    $env:PSX_DISRUPTOR_MODERN_CONTROLS = '1'
    $env:PSX_DISRUPTOR_MOUSE_AIM = '1'
    $env:PSX_DISRUPTOR_HIGH_PRECISION_CAMERA = if ($HighPrecisionCamera) { '1' } else { '0' }
    $env:PSX_GEOMETRY_CORRECTION = if ($GeometryCorrection) { '1' } else { '0' }
    $env:PSX_FPS_TELEMETRY = '0'
    $env:PSX_RUNTIME_PERF_DIAG = '1'
    $env:PSX_RUNTIME_PERF_DIAG_MS = '1000'
    $env:PSX_WS_FRUSTUM_MODE = 'full'
    $env:PSX_FRAME_INTERPOLATION = if ($InterpolationMode -eq 'adaptive') { '1' } else { '0' }
    $env:PSX_FRAME_INTERPOLATION_FPS = '0'
    $env:PSX_FRAME_INTERPOLATION_BLEND = 'adaptive'
    $env:PSX_GL_INTERP_DIAG = if ($InterpolationMode -eq 'adaptive') { '1' } else { '0' }
    Remove-Item Env:PSX_DISRUPTOR_MOUSE_SENSITIVITY -ErrorAction SilentlyContinue
    Remove-Item Env:PSX_DISRUPTOR_MOUSE_INVERT_X -ErrorAction SilentlyContinue

    if (Test-Path -LiteralPath $MouseAimLog) {
        Remove-Item -LiteralPath $MouseAimLog -Force
    }

    $RuntimeLog = Join-Path $Root "runtime-$RunKey.log"
    $RuntimeErrors = Join-Path $Root "runtime-$RunKey-errors.log"
    $SessionPath = Join-Path $Root "$RunKey-session.txt"
    $SavedControlLog = Join-Path $Root "disruptor-$RunKey.log"

    Write-Host 'Modern controls: WASD, LMB fire, RMB psionic, Space jump, E use.'
    Write-Host 'Middle-click in gameplay to capture; middle-click or Esc releases.'
    if ($IsWide) {
        Write-Host 'Corrected Test 9 widescreen and full discovered frustum enabled.'
    }
    if ($InterpolationMode -eq 'off') {
        Write-Host 'Presentation frame blending: OFF for a clean geometry/camera comparison.' -ForegroundColor Yellow
    }
    elseif ($InterpolationMode -eq 'adaptive') {
        Write-Host 'Presentation smoothing: motion-adaptive, targeting the display refresh rate.' -ForegroundColor Cyan
        Write-Host 'Guest logic, input, audio, and 8-bit yaw remain at their original cadence.'
    }
    else {
        Write-Host 'Presentation smoothing: LINEAR diagnostic comparison.' -ForegroundColor Yellow
        Write-Host 'Visible double images are possible; use this only if adaptive appears unchanged.'
    }

    $SystemInfoPath = Join-Path $Root 'system-info.txt'
    $SystemInfo = @(
        'DISRUPTOR MODERNISATION TEST 12 - SYSTEM INFORMATION',
        ('Collected UTC: {0}' -f (Get-Date).ToUniversalTime().ToString('o')),
        ('PowerShell: {0}' -f $PSVersionTable.PSVersion),
        ''
    )
    try {
        $OperatingSystem = Get-CimInstance Win32_OperatingSystem |
            Select-Object Caption, Version, BuildNumber, OSArchitecture,
                          @{Name='MemoryGiB'; Expression={[math]::Round($_.TotalVisibleMemorySize / 1MB, 2)}}
        $SystemInfo += 'OPERATING SYSTEM'
        $SystemInfo += ($OperatingSystem | Format-List | Out-String).TrimEnd()
        $SystemInfo += ''
    }
    catch {
        $SystemInfo += ('Operating-system query unavailable: {0}' -f $_.Exception.Message)
        $SystemInfo += ''
    }
    try {
        $Processors = Get-CimInstance Win32_Processor |
            Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed
        $SystemInfo += 'PROCESSOR'
        $SystemInfo += ($Processors | Format-List | Out-String).TrimEnd()
        $SystemInfo += ''
    }
    catch {
        $SystemInfo += ('Processor query unavailable: {0}' -f $_.Exception.Message)
        $SystemInfo += ''
    }
    try {
        $Graphics = Get-CimInstance Win32_VideoController |
            Select-Object Name, DriverVersion, DriverDate, VideoModeDescription,
                          CurrentHorizontalResolution, CurrentVerticalResolution,
                          CurrentRefreshRate
        $SystemInfo += 'GRAPHICS ADAPTERS'
        $SystemInfo += ($Graphics | Format-List | Out-String).TrimEnd()
        $SystemInfo += ''
    }
    catch {
        $SystemInfo += ('Graphics query unavailable: {0}' -f $_.Exception.Message)
        $SystemInfo += ''
    }
    try {
        $SystemInfo += 'ACTIVE POWER PLAN'
        $SystemInfo += ((& powercfg.exe /GetActiveScheme 2>&1 | Out-String).TrimEnd())
    }
    catch {
        $SystemInfo += ('Power-plan query unavailable: {0}' -f $_.Exception.Message)
    }
    $SystemInfo | Set-Content -LiteralPath $SystemInfoPath -Encoding UTF8

    @(
        'DISRUPTOR MODERNISATION TEST 12 - SESSION',
        ('Started UTC: {0}' -f (Get-Date).ToUniversalTime().ToString('o')),
        ('Mode: {0}' -f $ModeTitle),
        ('Mode key: {0}' -f $ExperimentMode),
        ('Disc file: {0}' -f (Split-Path -Leaf $Disc)),
        ('Game config: {0}' -f $ConfigName),
        'Keybind preset: keybinds-modern.ini',
        'Modern controls: True',
        ('Widescreen: {0}' -f $IsWide),
        'Frustum mode: full',
        ('Geometry correction: {0}' -f $GeometryCorrection),
        ('High-precision camera residual: {0}' -f $HighPrecisionCamera),
        ('Interpolation mode: {0}' -f $InterpolationMode),
        'Interpolation target: host display refresh',
        'Guest cadence: stock',
        'Renderer requested: opengl',
        'Supersampling requested: 4x'
    ) | Set-Content -LiteralPath $SessionPath -Encoding UTF8

    Push-Location $Root
    try {
        $RuntimeArguments = @(
            '--game', ('"{0}"' -f $GameConfig),
            '--disc', ('"{0}"' -f $Disc),
            '--renderer', 'opengl'
        )
        $StartParameters = @{
            FilePath = $Runtime
            ArgumentList = $RuntimeArguments
            WorkingDirectory = $Root
            RedirectStandardOutput = $RuntimeLog
            RedirectStandardError = $RuntimeErrors
            PassThru = $true
        }
        $RuntimeProcess = Start-Process @StartParameters
        $RuntimeProcess.WaitForExit()
        $RuntimeProcess.Refresh()
        $RuntimeExitCode = $null
        try {
            if ($RuntimeProcess.HasExited) {
                $RuntimeExitCode = $RuntimeProcess.ExitCode
            }
        }
        catch {
            # Windows PowerShell 5 can lose ExitCode for a GUI-subsystem process.
            # The populated logs and runtime self-report remain authoritative.
        }

        $RuntimeLogBytes = if (Test-Path -LiteralPath $RuntimeLog) {
            (Get-Item -LiteralPath $RuntimeLog).Length
        } else { -1 }
        $RuntimeErrorBytes = if (Test-Path -LiteralPath $RuntimeErrors) {
            (Get-Item -LiteralPath $RuntimeErrors).Length
        } else { -1 }
        $RuntimeExitCodeText = if ($null -eq $RuntimeExitCode) {
            'unavailable'
        } else {
            [string]$RuntimeExitCode
        }
        @(
            ('Finished UTC: {0}' -f (Get-Date).ToUniversalTime().ToString('o')),
            ('Runtime exit code: {0}' -f $RuntimeExitCodeText),
            ('Runtime stdout bytes: {0}' -f $RuntimeLogBytes),
            ('Runtime stderr bytes: {0}' -f $RuntimeErrorBytes)
        ) | Add-Content -LiteralPath $SessionPath -Encoding UTF8

        if (Test-Path -LiteralPath $MouseAimLog) {
            Copy-Item -LiteralPath $MouseAimLog -Destination $SavedControlLog -Force
        }

        if ($null -ne $RuntimeExitCode -and $RuntimeExitCode -ne 0) {
            throw "DisruptorRecompiled.exe exited with code $RuntimeExitCode."
        }
        if ($RuntimeLogBytes -le 0) {
            throw 'The runtime log was empty; keep the error log and report what happened.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch '[Keybinds] Loaded' -Quiet)) {
            throw 'The runtime did not confirm that keybinds.ini was loaded; keep the logs and collect results.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: supersampling 4x' -Quiet)) {
            throw 'The runtime did not confirm 4x supersampling; keep the logs and collect results.'
        }

        if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: widescreen 16:9 (GTE X-squash + stretched present + HUD squash' -Quiet)) {
            throw 'The runtime did not confirm corrected 16:9 widescreen; keep the logs and collect results.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: widescreen frustum mode full' -Quiet)) {
            throw 'The runtime did not confirm the full Test 9 frustum; keep the logs and collect results.'
        }
        if ($GeometryCorrection -and !(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: exact-provenance presentation geometry enabled' -Quiet)) {
            throw 'The runtime did not confirm exact-provenance presentation geometry.'
        }

        if (!(Test-Path -LiteralPath $MouseAimLog)) {
            throw 'The modern-controls log was not created; keep the runtime logs and report what happened.'
        }
        if (!(Select-String -LiteralPath $MouseAimLog -SimpleMatch 'DISRUPTOR MODERN CONTROLS AND HORIZONTAL MOUSE AIM v2' -Quiet)) {
            throw 'The executable did not confirm the modern-controls hook.'
        }
        if (!(Select-String -LiteralPath $MouseAimLog -SimpleMatch 'event=middle-capture' -Quiet)) {
            throw 'No mouse capture was recorded. Re-run, enter live gameplay, and middle-click once.'
        }

        if ($InterpolationMode -eq 'off') {
            if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: GL frame interpolation disabled' -Quiet)) {
                throw 'The A/B runtime did not confirm that interpolation was disabled.'
            }
        }
        else {
            if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: GL frame interpolation enabled:' -Quiet)) {
                throw 'The runtime did not enable presentation smoothing; keep the logs and collect results.'
            }
            if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch '(motion-adaptive blend)' -Quiet)) {
                throw 'The runtime did not confirm motion-adaptive blending.'
            }
            if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch 'psxrecomp: GL interpolation cadence:' -Quiet)) {
                throw 'No interpolation cadence was recorded. Run in live gameplay for at least ten seconds, then close normally.'
            }
        }

        Write-Host "Test saved: $ModeTitle" -ForegroundColor Green
        Write-Host 'When the requested comparison runs are complete, run Collect Modernisation Results.cmd.' -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
