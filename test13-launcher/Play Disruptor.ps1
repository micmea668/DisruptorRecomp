$ErrorActionPreference = 'Stop'

function Confirm-ReleaseIdentity {
    param(
        [Parameter(Mandatory = $true)][string]$Runtime,
        [Parameter(Mandatory = $true)][string]$BuildInfo,
        [Parameter(Mandatory = $true)][string]$Checksums
    )

    if (!(Test-Path -LiteralPath $BuildInfo -PathType Leaf)) {
        throw 'BUILD-INFO.txt is missing. Test 13 requires an identified source build.'
    }
    if (!(Test-Path -LiteralPath $Checksums -PathType Leaf)) {
        throw 'CHECKSUMS.sha256 is missing. Test 13 will not run an unverified executable.'
    }

    $BuildInfoText = Get-Content -LiteralPath $BuildInfo -Raw
    $CommitMatch = [regex]::Match(
        $BuildInfoText,
        '(?im)^\s*source_commit=([0-9a-f]{40})\s*$'
    )
    if (!$CommitMatch.Success) {
        throw 'BUILD-INFO.txt must contain source_commit=<40 lowercase hexadecimal characters>.'
    }

    $ExpectedHash = $null
    foreach ($Line in Get-Content -LiteralPath $Checksums) {
        $HashMatch = [regex]::Match(
            $Line,
            '^\s*([0-9A-Fa-f]{64})\s+\*?(.+?)\s*$'
        )
        if (!$HashMatch.Success) {
            continue
        }
        $ListedName = [IO.Path]::GetFileName($HashMatch.Groups[2].Value)
        if ($ListedName -ieq [IO.Path]::GetFileName($Runtime)) {
            $ExpectedHash = $HashMatch.Groups[1].Value.ToLowerInvariant()
            break
        }
    }
    if ($null -eq $ExpectedHash) {
        throw 'CHECKSUMS.sha256 has no entry for DisruptorRecompiled.exe.'
    }

    $ActualHash = (Get-FileHash -LiteralPath $Runtime -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualHash -ne $ExpectedHash) {
        throw 'DisruptorRecompiled.exe does not match CHECKSUMS.sha256.'
    }

    return $CommitMatch.Groups[1].Value
}

try {
    $Root = Split-Path -Parent $MyInvocation.MyCommand.Path
    $Runtime = Join-Path $Root 'DisruptorRecompiled.exe'
    $BuildInfo = Join-Path $Root 'BUILD-INFO.txt'
    $Checksums = Join-Path $Root 'CHECKSUMS.sha256'
    $DefaultCue = Join-Path $Root 'input\Disruptor (USA).cue'
    $DefaultBin = Join-Path $Root 'input\Disruptor (USA).bin'
    $MouseAimLog = Join-Path $Root 'disruptor-mouse-aim.log'

    if (!(Test-Path -LiteralPath $Runtime -PathType Leaf)) {
        throw 'DisruptorRecompiled.exe is missing from this folder.'
    }
    $SourceCommit = Confirm-ReleaseIdentity -Runtime $Runtime `
        -BuildInfo $BuildInfo -Checksums $Checksums

    $Mode = [string]$env:DISRUPTOR_EXPERIMENT_MODE
    if (@('baseline', 'exact-geometry', 'x-only-camera', 'full-xy-camera', 'coverage-tint') -notcontains $Mode) {
        throw "Unknown Test 13 mode: $Mode"
    }

    $GeometryCorrection = $Mode -ne 'baseline'
    $HighPrecisionCamera = @('x-only-camera', 'full-xy-camera') -contains $Mode
    $FullYaw = $Mode -eq 'full-xy-camera'
    $CoverageTint = $Mode -eq 'coverage-tint'
    $ModeTitle = switch ($Mode) {
        'baseline'        { 'Test 9 baseline' }
        'exact-geometry'  { 'exact-provenance geometry' }
        'x-only-camera'   { 'exact geometry + current X-only fractional yaw control' }
        'full-xy-camera'  { 'exact geometry + full X/Y fractional yaw' }
        'coverage-tint'   { 'exact-geometry acceptance coverage tint' }
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

    foreach ($RequiredName in @(
        'game-widescreen.toml',
        'keybinds-modern.ini',
        'mouse-aim.ini'
    )) {
        if (!(Test-Path -LiteralPath (Join-Path $Root $RequiredName) -PathType Leaf)) {
            throw "$RequiredName is missing from this folder."
        }
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $Root 'saves') | Out-Null
    Copy-Item -LiteralPath (Join-Path $Root 'keybinds-modern.ini') `
        -Destination (Join-Path $Root 'keybinds.ini') -Force

    $env:PSX_DISRUPTOR_CONTROL_PROBE = '0'
    $env:PSX_DISRUPTOR_MODERN_CONTROLS = '1'
    $env:PSX_DISRUPTOR_MOUSE_AIM = '1'
    $env:PSX_DISRUPTOR_HIGH_PRECISION_CAMERA = if ($HighPrecisionCamera) { '1' } else { '0' }
    $env:PSX_GEOMETRY_CORRECTION = if ($GeometryCorrection) { '1' } else { '0' }
    $env:PSX_GEOMETRY_FULL_YAW = if ($FullYaw) { '1' } else { '0' }
    $env:PSX_GEOMETRY_COVERAGE_TINT = if ($CoverageTint) { '1' } else { '0' }
    $env:PSX_FPS_TELEMETRY = '0'
    $env:PSX_RUNTIME_PERF_DIAG = '1'
    $env:PSX_RUNTIME_PERF_DIAG_MS = '1000'
    $env:PSX_WS_FRUSTUM_MODE = 'full'
    $env:PSX_FRAME_INTERPOLATION = '0'
    $env:PSX_FRAME_INTERPOLATION_FPS = '0'
    $env:PSX_GL_INTERP_DIAG = '0'
    Remove-Item Env:PSX_DISRUPTOR_MOUSE_SENSITIVITY -ErrorAction SilentlyContinue
    Remove-Item Env:PSX_DISRUPTOR_MOUSE_INVERT_X -ErrorAction SilentlyContinue

    if (Test-Path -LiteralPath $MouseAimLog) {
        Remove-Item -LiteralPath $MouseAimLog -Force
    }
    $RuntimeLog = Join-Path $Root "runtime-$Mode.log"
    $RuntimeErrors = Join-Path $Root "runtime-$Mode-errors.log"
    $SessionPath = Join-Path $Root "$Mode-session.txt"

    @(
        'DISRUPTOR MODERNISATION TEST 13 - SESSION',
        ('Started UTC: {0}' -f (Get-Date).ToUniversalTime().ToString('o')),
        ('Mode: {0}' -f $ModeTitle),
        ('Mode key: {0}' -f $Mode),
        ('Source commit: {0}' -f $SourceCommit),
        ('Disc file: {0}' -f (Split-Path -Leaf $Disc)),
        ('Geometry correction: {0}' -f $GeometryCorrection),
        ('High-precision camera residual: {0}' -f $HighPrecisionCamera),
        ('Full X/Y yaw: {0}' -f $FullYaw),
        ('Coverage tint: {0}' -f $CoverageTint),
        'Interpolation: False',
        'Renderer requested: opengl',
        'Supersampling requested: 4x'
    ) | Set-Content -LiteralPath $SessionPath -Encoding UTF8

    Write-Host "Test 13 mode: $ModeTitle" -ForegroundColor Cyan
    Write-Host "Verified source identity: $SourceCommit"
    Write-Host 'Interpolation is OFF so this run isolates geometry and fractional yaw.' -ForegroundColor Yellow
    Write-Host 'Enter live gameplay, middle-click to capture the mouse, then inspect the same route as the other modes.'

    Push-Location $Root
    try {
        $RuntimeArguments = @(
            '--game', ('"{0}"' -f (Join-Path $Root 'game-widescreen.toml')),
            '--disc', ('"{0}"' -f $Disc),
            '--renderer', 'opengl'
        )
        $RuntimeProcess = Start-Process -FilePath $Runtime `
            -ArgumentList $RuntimeArguments `
            -WorkingDirectory $Root `
            -RedirectStandardOutput $RuntimeLog `
            -RedirectStandardError $RuntimeErrors `
            -PassThru
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
        }

        if ($null -ne $RuntimeExitCode -and $RuntimeExitCode -ne 0) {
            throw "DisruptorRecompiled.exe exited with code $RuntimeExitCode."
        }
        if (!(Test-Path -LiteralPath $RuntimeLog) -or
            (Get-Item -LiteralPath $RuntimeLog).Length -le 0) {
            throw 'The runtime log was empty; keep the error log and report what happened.'
        }
        if (Select-String -LiteralPath $RuntimeLog -Pattern `
                'psxrecomp: GL pipeline init failed.*falling back to software renderer' -Quiet) {
            throw 'OpenGL initialization failed and the runtime fell back to software; this Test 13 run is invalid.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog `
                -SimpleMatch 'psxrecomp: GL GPU pipeline ready (internal scale 4x' -Quiet)) {
            throw 'The runtime did not confirm that the 4x OpenGL GPU pipeline became ready.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog -SimpleMatch '[Keybinds] Loaded' -Quiet)) {
            throw 'The runtime did not confirm that keybinds.ini was loaded.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog `
                -SimpleMatch 'psxrecomp: widescreen frustum mode full' -Quiet)) {
            throw 'The runtime did not confirm the full Test 9 frustum.'
        }
        if (!(Select-String -LiteralPath $RuntimeLog `
                -SimpleMatch 'psxrecomp: GL frame interpolation disabled' -Quiet)) {
            throw 'The runtime did not confirm that frame interpolation was disabled.'
        }
        if ($GeometryCorrection -and
            !(Select-String -LiteralPath $RuntimeLog `
                -SimpleMatch 'psxrecomp: exact-provenance presentation geometry enabled' -Quiet)) {
            throw 'The runtime did not confirm exact-provenance presentation geometry.'
        }
        if (!$GeometryCorrection -and
            (Select-String -LiteralPath $RuntimeLog `
                -SimpleMatch 'psxrecomp: exact-provenance presentation geometry enabled' -Quiet)) {
            throw 'The baseline unexpectedly enabled presentation geometry correction.'
        }
        if ($FullYaw -and
            !(Select-String -LiteralPath $RuntimeLog -SimpleMatch `
                'psxrecomp: geometry diagnostics: full X/Y yaw enabled, exact-coverage tint disabled' -Quiet)) {
            throw 'The runtime did not confirm full X/Y yaw with coverage tint disabled.'
        }
        if ($CoverageTint -and
            !(Select-String -LiteralPath $RuntimeLog -SimpleMatch `
                'psxrecomp: geometry diagnostics: full X/Y yaw disabled, exact-coverage tint enabled' -Quiet)) {
            throw 'The runtime did not confirm coverage tint with full X/Y yaw disabled.'
        }
        if (!(Test-Path -LiteralPath $MouseAimLog)) {
            throw 'The modern-controls log was not created.'
        }
        if (!(Select-String -LiteralPath $MouseAimLog -SimpleMatch `
                'DISRUPTOR MODERN CONTROLS AND HORIZONTAL MOUSE AIM v2' -Quiet)) {
            throw 'The executable did not confirm the modern-controls hook.'
        }
        $ExpectedCameraSetting = if ($HighPrecisionCamera) {
            'high_precision_camera=true'
        } else {
            'high_precision_camera=false'
        }
        if (!(Select-String -LiteralPath $MouseAimLog `
                -SimpleMatch $ExpectedCameraSetting -Quiet)) {
            throw "The controls log did not confirm $ExpectedCameraSetting."
        }
        if (!(Select-String -LiteralPath $MouseAimLog `
                -SimpleMatch 'event=middle-capture' -Quiet)) {
            throw 'No live-gameplay mouse capture was recorded; repeat the run and middle-click in gameplay.'
        }
        if ($GeometryCorrection -and
            !(Select-String -LiteralPath $MouseAimLog -Pattern `
                'geometry_diag .*scope=latest_live .*latest_live_valid=1' -Quiet)) {
            throw 'No live corrected-presentation diagnostic sample was recorded; remain in gameplay for at least one second.'
        }

        Copy-Item -LiteralPath $MouseAimLog `
            -Destination (Join-Path $Root "disruptor-$Mode.log") -Force
        Write-Host "Valid Test 13 run saved: $ModeTitle" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
