$ErrorActionPreference = 'Stop'

try {
    $Root = Split-Path -Parent $MyInvocation.MyCommand.Path
    $GeometryControlLog = Join-Path $Root 'disruptor-exact-geometry.log'
    $GeometryRuntimeLog = Join-Path $Root 'runtime-exact-geometry.log'
    $CameraControlLog = Join-Path $Root 'disruptor-exact-camera.log'
    $CameraRuntimeLog = Join-Path $Root 'runtime-exact-camera.log'
    $AdaptiveRuntimeLog = Join-Path $Root 'runtime-exact-camera-adaptive.log'
    $BaselineRuntimeLog = Join-Path $Root 'runtime-baseline.log'

    if (!(Test-Path -LiteralPath $GeometryControlLog)) {
        throw 'The exact-geometry controls log was not found. Run the Exact Geometry Only launcher first.'
    }
    if (!(Test-Path -LiteralPath $GeometryRuntimeLog)) {
        throw 'The exact-geometry runtime log was not found. Run the Exact Geometry Only launcher first.'
    }
    if (!(Select-String -LiteralPath $GeometryControlLog -SimpleMatch 'DISRUPTOR MODERN CONTROLS AND HORIZONTAL MOUSE AIM v2' -Quiet)) {
        throw 'The exact-geometry control log does not contain the expected build header.'
    }
    if (!(Select-String -LiteralPath $GeometryControlLog -SimpleMatch 'event=middle-capture' -Quiet)) {
        Write-Host 'Warning: no mouse capture was recorded; collecting the partial result.' -ForegroundColor Yellow
    }
    if (!(Select-String -LiteralPath $GeometryRuntimeLog -SimpleMatch '[Keybinds] Loaded' -Quiet)) {
        throw 'The exact-geometry runtime did not confirm that keybinds.ini was loaded.'
    }
    if (!(Select-String -LiteralPath $GeometryRuntimeLog -SimpleMatch 'psxrecomp: widescreen frustum mode full' -Quiet)) {
        Write-Host 'Warning: the exact-geometry runtime did not confirm the full Test 9 frustum.' -ForegroundColor Yellow
    }
    if (!(Select-String -LiteralPath $GeometryRuntimeLog -SimpleMatch 'psxrecomp: exact-provenance presentation geometry enabled' -Quiet)) {
        Write-Host 'Warning: the exact-geometry runtime did not confirm exact-provenance geometry.' -ForegroundColor Yellow
    }
    if (!(Select-String -LiteralPath $GeometryRuntimeLog -SimpleMatch 'psxrecomp: GL frame interpolation disabled' -Quiet)) {
        Write-Host 'Warning: the exact-geometry runtime did not confirm that frame blending was disabled.' -ForegroundColor Yellow
    }

    if (!(Test-Path -LiteralPath $CameraRuntimeLog)) {
        Write-Host 'Warning: the fractional-camera comparison is missing; collecting the geometry-first result.' -ForegroundColor Yellow
    }
    else {
        if (!(Select-String -LiteralPath $CameraRuntimeLog -SimpleMatch 'psxrecomp: exact-provenance presentation geometry enabled' -Quiet)) {
            Write-Host 'Warning: the fractional-camera runtime did not confirm exact-provenance geometry.' -ForegroundColor Yellow
        }
        if (!(Select-String -LiteralPath $CameraRuntimeLog -SimpleMatch 'psxrecomp: GL frame interpolation disabled' -Quiet)) {
            Write-Host 'Warning: the fractional-camera runtime did not confirm that frame blending was disabled.' -ForegroundColor Yellow
        }
        if ((Test-Path -LiteralPath $CameraControlLog) -and
            !(Select-String -LiteralPath $CameraControlLog -SimpleMatch 'high_precision_camera=true' -Quiet)) {
            Write-Host 'Warning: the fractional-camera control log did not confirm its residual path.' -ForegroundColor Yellow
        }
    }
    if (!(Test-Path -LiteralPath $BaselineRuntimeLog)) {
        Write-Host 'Warning: the Test 9 baseline run is missing; collecting the available runs.' -ForegroundColor Yellow
    }
    elseif (!(Select-String -LiteralPath $BaselineRuntimeLog -SimpleMatch 'psxrecomp: GL frame interpolation disabled' -Quiet)) {
        Write-Host 'Warning: the A/B runtime did not confirm that interpolation was disabled.' -ForegroundColor Yellow
    }

    if (Test-Path -LiteralPath $AdaptiveRuntimeLog) {
        if (!(Select-String -LiteralPath $AdaptiveRuntimeLog -SimpleMatch 'psxrecomp: GL interpolation cadence:' -Quiet)) {
            Write-Host 'Warning: the optional adaptive run did not record interpolation cadence.' -ForegroundColor Yellow
        }
    }

    $Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $Destination = Join-Path $Root "Disruptor-Modernisation-Test-12-Results-$Stamp.zip"
    $Paths = New-Object System.Collections.Generic.List[string]

    foreach ($Name in @(
        'system-info.txt',
        'mouse-aim.ini',
        'input.ini',
        'keybinds.ini',
        'keybinds-modern.ini',
        'keybinds-original.ini',
        'game-widescreen.toml',
        'game-4-3.toml',
        'BUILD-INFO.txt',
        'CHECKSUMS.sha256',
        'overlay_captures.json',
        'overlay_captures.json.d',
        'overlay_captures.addendum.jsonl',
        'capture_history',
        'psx_last_run_report.json',
        'psx_crash.txt'
    )) {
        $Path = Join-Path $Root $Name
        if (Test-Path -LiteralPath $Path) {
            $Paths.Add($Path)
        }
    }

    foreach ($Pattern in @(
        'runtime-*.log',
        '*-session.txt',
        'disruptor-*.log',
        'psx_freeze_dump*.json',
        'starvation_dump*.jsonl'
    )) {
        Get-ChildItem -LiteralPath $Root -Filter $Pattern -File -ErrorAction SilentlyContinue |
            ForEach-Object { $Paths.Add($_.FullName) }
    }

    if ($Paths.Count -eq 0) {
        throw 'No result files were found.'
    }

    $UniquePaths = @($Paths.ToArray() | Sort-Object -Unique)
    Compress-Archive -LiteralPath $UniquePaths -DestinationPath $Destination
    Write-Host "Created: $Destination" -ForegroundColor Green
    Write-Host ''
    Write-Host 'Upload this ZIP privately in our ChatGPT conversation.'
    Write-Host 'Please describe geometry stability, smallest turns, cracks/warping, HUD/doors/enemies, performance, and audio.'
    Write-Host 'Do not publish it or add it to GitHub; capture history may contain retail game code.' -ForegroundColor Yellow
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
