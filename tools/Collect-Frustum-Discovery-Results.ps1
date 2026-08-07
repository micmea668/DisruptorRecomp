$ErrorActionPreference = 'Stop'

try {
    $Root = Split-Path -Parent $MyInvocation.MyCommand.Path
    $CapturePath = Join-Path $Root 'Disruptor-main-code-private.bin'
    $MetadataPath = Join-Path $Root 'frustum-capture.json'
    $StatusPath = Join-Path $Root 'frustum-capture-status.txt'
    $ExpectedExecutableSha256 = '48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4'
    $ExpectedCodeLength = 284472
    $Verified = $false

    if ((Test-Path -LiteralPath $CapturePath) -and (Test-Path -LiteralPath $MetadataPath)) {
        $Metadata = Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json
        $ActualCodeLength = (Get-Item -LiteralPath $CapturePath).Length
        $ActualCodeSha256 = (Get-FileHash -LiteralPath $CapturePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Metadata.result -eq 'success' -and
            $Metadata.format -eq 'disruptor-frustum-discovery-v2' -and
            $Metadata.executableSha256 -eq $ExpectedExecutableSha256 -and
            $ActualCodeLength -eq $ExpectedCodeLength -and
            $Metadata.length -eq $ExpectedCodeLength -and
            $ActualCodeSha256 -eq $Metadata.sha256) {
            $Verified = $true
            Write-Host 'Verified private frustum-analysis capture found.' -ForegroundColor Green
        }
        else {
            Write-Host 'Warning: the capture or metadata did not pass every identity check.' -ForegroundColor Yellow
        }
    }
    else {
        Write-Host 'Warning: no complete private capture was found; collecting the status file only.' -ForegroundColor Yellow
    }

    $Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $Destination = Join-Path $Root "Disruptor-Modernisation-Test-8B-Corrected-Frustum-Discovery-Results-$Stamp.zip"
    $Paths = New-Object System.Collections.Generic.List[string]
    foreach ($Name in @(
        'Disruptor-main-code-private.bin',
        'frustum-capture.json',
        'frustum-capture-status.txt',
        'BUILD-INFO.txt',
        'CHECKSUMS.sha256'
    )) {
        $Path = Join-Path $Root $Name
        if (Test-Path -LiteralPath $Path) {
            $Paths.Add($Path)
        }
    }
    if ($Paths.Count -eq 0) {
        throw 'No discovery result files were found.'
    }

    Compress-Archive -LiteralPath @($Paths.ToArray() | Sort-Object -Unique) -DestinationPath $Destination
    Write-Host "Created: $Destination" -ForegroundColor Green
    if (!$Verified) {
        Write-Host 'The result is incomplete; upload it anyway so the failure can be diagnosed.' -ForegroundColor Yellow
    }
    Write-Host ''
    Write-Host 'Upload this ZIP privately in our ChatGPT conversation.'
    Write-Host 'PRIVATE: it may contain raw retail-derived code from your disc.' -ForegroundColor Yellow
    Write-Host 'Do not publish, redistribute, or add it to GitHub.' -ForegroundColor Yellow
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
