$ErrorActionPreference = 'Stop'

$Root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$Framework = Join-Path $Root 'psxrecomp'
$Pin       = (Get-Content (Join-Path $Root 'PSXRECOMP_PIN') -Raw).Trim()
$Exe       = Join-Path $Root 'input/SLUS_002.24'

function Invoke-Checked {
    param([scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Resolve-BuiltTool {
    param(
        [string]$BuildDirectory,
        [string]$Name
    )
    $Candidates = @(
        (Join-Path $BuildDirectory "$Name.exe"),
        (Join-Path $BuildDirectory "Release/$Name.exe"),
        (Join-Path $BuildDirectory $Name),
        (Join-Path $BuildDirectory "Release/$Name")
    )
    $Tool = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (!$Tool) { throw "Built tool was not found: $Name" }
    return $Tool
}

foreach ($Command in @('git', 'cmake', 'python')) {
    if (!(Get-Command $Command -ErrorAction SilentlyContinue)) {
        throw "Missing prerequisite: $Command"
    }
}
if (!(Test-Path $Exe)) {
    throw "Copy the verified SLUS_002.24 into input/ before building."
}

if (!(Test-Path (Join-Path $Framework '.git'))) {
    Invoke-Checked { git clone https://github.com/mstan/psxrecomp.git $Framework }
}
Invoke-Checked { git -C $Framework fetch --quiet origin $Pin }
Invoke-Checked { git -C $Framework checkout --detach $Pin }
Invoke-Checked {
    python (Join-Path $Root 'tools/apply_framework_overlay.py') --framework $Framework
}
Invoke-Checked {
    python (Join-Path $Root 'tools/patch_openbios_seeds.py') --framework $Framework
}

$Generator = @()
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $Generator = @('-G', 'Ninja')
}

$RecompilerBuild = Join-Path $Framework 'recompiler/build'
Invoke-Checked {
    cmake -S (Join-Path $Framework 'recompiler') -B $RecompilerBuild @Generator `
        -DCMAKE_BUILD_TYPE=Release
}
Invoke-Checked {
    cmake --build $RecompilerBuild --config Release --parallel `
        --target psxrecomp-game psxrecomp-bios
}

Push-Location $Root
try {
    Invoke-Checked { python tools/inspect_exe.py }
    Invoke-Checked { python tools/extract_code_image.py }
    Invoke-Checked { python tools/generate_seeds.py }
    $GameTool = Resolve-BuiltTool $RecompilerBuild 'psxrecomp-game'
    Invoke-Checked { & $GameTool --config game.toml }
    Invoke-Checked { python tools/audit_codegen.py }
}
finally {
    Pop-Location
}

Push-Location $Framework
try {
    $BiosTool = Resolve-BuiltTool $RecompilerBuild 'psxrecomp-bios'
    Invoke-Checked { & $BiosTool --config bios/OpenBIOS.toml }
}
finally {
    Pop-Location
}

$Build = Join-Path $Root 'build'
Invoke-Checked {
    cmake -S $Root -B $Build @Generator -DCMAKE_BUILD_TYPE=Release `
        -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
}
Invoke-Checked { cmake --build $Build --config Release --parallel --target psx-runtime }

Write-Host ""
Write-Host "Build complete. Run .\run.ps1 after placing the matching BIN/CUE in input/."
