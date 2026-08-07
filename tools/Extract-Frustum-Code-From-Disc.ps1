param(
    [string]$DiscPath = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

# Discovery output always belongs beside this script. Keep the legacy parameter
# so an older CMD wrapper cannot fail parameter binding, but never trust or
# normalize its value: a quoted trailing backslash can survive CMD parsing as
# an illegal quote character on Windows PowerShell 5.
$OutputDirectory = $PSScriptRoot

$OutputPath = Join-Path $OutputDirectory 'Disruptor-main-code-private.bin'
$MetadataPath = Join-Path $OutputDirectory 'frustum-capture.json'
$StatusPath = Join-Path $OutputDirectory 'frustum-capture-status.txt'

$ExpectedBinSize = [Int64]636350064
$ExpectedBinSha256 = '3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4'
$ExpectedExeSize = 401408
$ExpectedExeSha256 = '48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4'
$ExpectedEntryPc = [Convert]::ToUInt32('80048CE4', 16)
$ExpectedLoadAddress = [Convert]::ToUInt32('80010000', 16)
$ExpectedTextSize = [Convert]::ToUInt32('00061800', 16)
$CodeStartAddress = [Convert]::ToUInt32('80011200', 16)
$CodeEndAddress = [Convert]::ToUInt32('80056938', 16)
$CodeLength = [int]($CodeEndAddress - $CodeStartAddress)

function Set-DiscoveryStatus {
    param([string]$Message)

    @(
        'DISRUPTOR MODERNISATION TEST 8B - CORRECTED FRUSTUM DISCOVERY',
        ('Updated UTC: {0}' -f [DateTime]::UtcNow.ToString('o')),
        $Message
    ) | Set-Content -LiteralPath $StatusPath -Encoding UTF8
}

function Get-BytesSha256 {
    param([byte[]]$Bytes)

    $Hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        [byte[]]$Digest = $Hasher.ComputeHash($Bytes)
    }
    finally {
        $Hasher.Dispose()
    }
    return -join ($Digest | ForEach-Object { $_.ToString('x2') })
}

function Read-ExactlyAt {
    param(
        [System.IO.FileStream]$Stream,
        [Int64]$Offset,
        [int]$Length
    )

    if ($Offset -lt 0 -or $Length -lt 0 -or ($Offset + $Length) -gt $Stream.Length) {
        throw "Disc read is outside the BIN: offset=$Offset length=$Length."
    }
    $Stream.Position = $Offset
    [byte[]]$Buffer = New-Object byte[] $Length
    $Total = 0
    while ($Total -lt $Length) {
        $Read = $Stream.Read($Buffer, $Total, $Length - $Total)
        if ($Read -le 0) {
            throw "Unexpected end of BIN after $Total of $Length bytes."
        }
        $Total += $Read
    }
    return ,$Buffer
}

function Read-Mode2Form1Sector {
    param(
        [System.IO.FileStream]$Stream,
        [uint32]$Lba
    )

    $RawOffset = [Int64]$Lba * 2352
    [byte[]]$Raw = Read-ExactlyAt -Stream $Stream -Offset $RawOffset -Length 2352
    if ($Raw[15] -ne 2) {
        throw "Logical block $Lba is not a MODE2 sector."
    }
    if (($Raw[18] -band 0x20) -ne 0) {
        throw "Logical block $Lba is MODE2 Form 2; ISO data requires Form 1."
    }
    [byte[]]$UserData = New-Object byte[] 2048
    [Array]::Copy($Raw, 24, $UserData, 0, 2048)
    return ,$UserData
}

function Read-Mode2Form1Extent {
    param(
        [System.IO.FileStream]$Stream,
        [uint32]$StartLba,
        [int]$Length
    )

    [byte[]]$Output = New-Object byte[] $Length
    $Written = 0
    $SectorIndex = 0
    while ($Written -lt $Length) {
        [byte[]]$Sector = Read-Mode2Form1Sector -Stream $Stream -Lba ($StartLba + $SectorIndex)
        $CopyLength = [Math]::Min(2048, $Length - $Written)
        [Array]::Copy($Sector, 0, $Output, $Written, $CopyLength)
        $Written += $CopyLength
        $SectorIndex++
    }
    return ,$Output
}

function Resolve-BinPath {
    param([string]$SelectedPath)

    if ([string]::IsNullOrWhiteSpace($SelectedPath)) {
        $DefaultCue = Join-Path $OutputDirectory 'input\Disruptor (USA).cue'
        $DefaultBin = Join-Path $OutputDirectory 'input\Disruptor (USA).bin'
        if ((Test-Path -LiteralPath $DefaultCue) -and (Test-Path -LiteralPath $DefaultBin)) {
            $SelectedPath = $DefaultCue
        }
        else {
            Add-Type -AssemblyName System.Windows.Forms
            $Picker = New-Object System.Windows.Forms.OpenFileDialog
            $Picker.Title = 'Select your Disruptor (USA) CUE or BIN file'
            $Picker.Filter = 'PlayStation disc (*.cue;*.bin)|*.cue;*.bin|Cue sheet (*.cue)|*.cue|Raw disc track (*.bin)|*.bin|All files (*.*)|*.*'
            $Picker.CheckFileExists = $true
            if ($Picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
                throw 'No disc file was selected.'
            }
            $SelectedPath = $Picker.FileName
        }
    }

    $SelectedPath = (Resolve-Path -LiteralPath $SelectedPath).Path
    $Extension = [System.IO.Path]::GetExtension($SelectedPath).ToLowerInvariant()
    if ($Extension -eq '.bin') {
        return $SelectedPath
    }
    if ($Extension -ne '.cue') {
        throw 'Select the original Disruptor CUE or MODE2/2352 BIN file.'
    }

    $CueText = Get-Content -LiteralPath $SelectedPath -Raw
    $FileMatch = [regex]::Match(
        $CueText,
        '(?im)^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+BINARY\s*$'
    )
    if (!$FileMatch.Success) {
        throw 'The CUE does not contain a FILE ... BINARY entry.'
    }
    $BinName = if ($FileMatch.Groups[1].Success) {
        $FileMatch.Groups[1].Value
    }
    else {
        $FileMatch.Groups[2].Value
    }
    $ResolvedBin = Join-Path (Split-Path -Parent $SelectedPath) $BinName
    if (!(Test-Path -LiteralPath $ResolvedBin)) {
        throw "The BIN named by the CUE is missing: $ResolvedBin"
    }
    return (Resolve-Path -LiteralPath $ResolvedBin).Path
}

try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    foreach ($OldPath in @($OutputPath, $MetadataPath, $StatusPath)) {
        if (Test-Path -LiteralPath $OldPath) {
            Remove-Item -LiteralPath $OldPath -Force
        }
    }

    Set-DiscoveryStatus 'Waiting for the original Disruptor CUE or MODE2/2352 BIN.'
    $BinPath = Resolve-BinPath -SelectedPath $DiscPath
    $BinInfo = Get-Item -LiteralPath $BinPath
    if ($BinInfo.Length -ne $ExpectedBinSize) {
        throw "Unsupported BIN size: expected $ExpectedBinSize bytes, got $($BinInfo.Length)."
    }

    Set-DiscoveryStatus 'Hashing the original BIN before extraction. This may take several seconds.'
    $BinSha256 = (Get-FileHash -LiteralPath $BinPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($BinSha256 -ne $ExpectedBinSha256) {
        throw "Unsupported BIN hash: expected $ExpectedBinSha256, got $BinSha256."
    }

    $Stream = [System.IO.File]::Open(
        $BinPath,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )
    try {
        Set-DiscoveryStatus 'Disc verified; locating the ISO9660 root directory.'
        $PrimaryVolumeLba = $null
        [byte[]]$PrimaryVolume = $null
        for ($Lba = 16; $Lba -le 31; $Lba++) {
            [byte[]]$Candidate = Read-Mode2Form1Sector -Stream $Stream -Lba $Lba
            $Identifier = [System.Text.Encoding]::ASCII.GetString($Candidate, 1, 5)
            if ($Candidate[0] -eq 1 -and $Identifier -eq 'CD001' -and $Candidate[6] -eq 1) {
                $PrimaryVolumeLba = $Lba
                $PrimaryVolume = $Candidate
                break
            }
        }
        if ($null -eq $PrimaryVolumeLba) {
            throw 'Could not locate the ISO9660 primary volume descriptor.'
        }

        $RootRecordOffset = 156
        $RootRecordLength = [int]$PrimaryVolume[$RootRecordOffset]
        if ($RootRecordLength -lt 34) {
            throw 'The ISO9660 root directory record is invalid.'
        }
        $RootExtentLba = [BitConverter]::ToUInt32($PrimaryVolume, $RootRecordOffset + 2)
        $RootExtentLength = [BitConverter]::ToUInt32($PrimaryVolume, $RootRecordOffset + 10)
        if ($RootExtentLength -lt 34 -or $RootExtentLength -gt 1048576) {
            throw "The ISO9660 root directory length is invalid: $RootExtentLength."
        }
        [byte[]]$RootDirectory = Read-Mode2Form1Extent -Stream $Stream -StartLba $RootExtentLba -Length ([int]$RootExtentLength)

        $ExecutableExtentLba = $null
        $ExecutableLength = $null
        $Position = 0
        while ($Position -lt $RootDirectory.Length) {
            $RecordLength = [int]$RootDirectory[$Position]
            if ($RecordLength -eq 0) {
                $Position = ([int]([Math]::Floor($Position / 2048)) + 1) * 2048
                continue
            }
            if ($RecordLength -lt 34 -or ($Position + $RecordLength) -gt $RootDirectory.Length) {
                throw "Invalid ISO9660 directory record at byte $Position."
            }
            $NameLength = [int]$RootDirectory[$Position + 32]
            if (($Position + 33 + $NameLength) -gt ($Position + $RecordLength)) {
                throw "Invalid ISO9660 filename at byte $Position."
            }
            $Name = [System.Text.Encoding]::ASCII.GetString($RootDirectory, $Position + 33, $NameLength)
            if ($Name.Equals('SLUS_002.24;1', [System.StringComparison]::OrdinalIgnoreCase)) {
                $ExecutableExtentLba = [BitConverter]::ToUInt32($RootDirectory, $Position + 2)
                $ExecutableLength = [BitConverter]::ToUInt32($RootDirectory, $Position + 10)
                break
            }
            $Position += $RecordLength
        }
        if ($null -eq $ExecutableExtentLba) {
            throw 'SLUS_002.24;1 was not found in the disc root directory.'
        }
        if ($ExecutableLength -ne $ExpectedExeSize) {
            throw "Unsupported SLUS_002.24 length: expected $ExpectedExeSize, got $ExecutableLength."
        }

        Set-DiscoveryStatus 'Found SLUS_002.24; extracting and verifying the supported executable.'
        [byte[]]$Executable = Read-Mode2Form1Extent -Stream $Stream -StartLba $ExecutableExtentLba -Length ([int]$ExecutableLength)
    }
    finally {
        $Stream.Dispose()
    }

    $ExecutableSha256 = Get-BytesSha256 -Bytes $Executable
    if ($ExecutableSha256 -ne $ExpectedExeSha256) {
        throw "Unsupported SLUS_002.24 hash: expected $ExpectedExeSha256, got $ExecutableSha256."
    }
    if ([System.Text.Encoding]::ASCII.GetString($Executable, 0, 8) -ne 'PS-X EXE') {
        throw 'The extracted SLUS_002.24 is not a PS-X EXE.'
    }
    $EntryPc = [BitConverter]::ToUInt32($Executable, 0x10)
    $LoadAddress = [BitConverter]::ToUInt32($Executable, 0x18)
    $TextSize = [BitConverter]::ToUInt32($Executable, 0x1C)
    if ($EntryPc -ne $ExpectedEntryPc -or
        $LoadAddress -ne $ExpectedLoadAddress -or
        $TextSize -ne $ExpectedTextSize) {
        throw ('Unsupported PS-X EXE header: entry=0x{0:X8}, load=0x{1:X8}, text=0x{2:X8}.' -f $EntryPc, $LoadAddress, $TextSize)
    }

    $CodeFileOffset = 0x800 + [int]($CodeStartAddress - $LoadAddress)
    if (($CodeFileOffset + $CodeLength) -gt $Executable.Length) {
        throw 'The configured main-code range is outside the extracted executable.'
    }
    [byte[]]$CodeBytes = New-Object byte[] $CodeLength
    [Array]::Copy($Executable, $CodeFileOffset, $CodeBytes, 0, $CodeLength)
    $CodeSha256 = Get-BytesSha256 -Bytes $CodeBytes
    [System.IO.File]::WriteAllBytes($OutputPath, $CodeBytes)

    [ordered]@{
        format = 'disruptor-frustum-discovery-v2'
        result = 'success'
        capturedUtc = [DateTime]::UtcNow.ToString('o')
        method = 'verified-mode2-disc-extraction'
        discFile = [System.IO.Path]::GetFileName($BinPath)
        discSize = $BinInfo.Length
        discSha256 = $BinSha256
        primaryVolumeLba = $PrimaryVolumeLba
        executableExtentLba = $ExecutableExtentLba
        executableLength = $ExecutableLength
        executableSha256 = $ExecutableSha256
        executableEntryPc = ('0x{0:X8}' -f $EntryPc)
        executableLoadAddress = ('0x{0:X8}' -f $LoadAddress)
        address = ('0x{0:X8}' -f $CodeStartAddress)
        length = $CodeLength
        sha256 = $CodeSha256
        privacy = 'Retail-derived code from the user-supplied disc. Keep private; never publish or commit to GitHub.'
    } | ConvertTo-Json | Set-Content -LiteralPath $MetadataPath -Encoding UTF8

    Set-DiscoveryStatus "SUCCESS: extracted $CodeLength verified code bytes for private frustum analysis."
    Write-Host 'Verified frustum-analysis capture created.' -ForegroundColor Green
    Write-Host "Code SHA-256: $CodeSha256"
    Write-Host 'Now run Collect Frustum Discovery Results.cmd.' -ForegroundColor Green
}
catch {
    Set-DiscoveryStatus "FAILED: $($_.Exception.Message)"
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host 'Keep frustum-capture-status.txt and report the failure.' -ForegroundColor Yellow
    exit 1
}
