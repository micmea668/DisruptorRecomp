$ErrorActionPreference = 'Stop'

$Root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$Framework = Join-Path $Root 'psxrecomp'
$Pin       = (Get-Content (Join-Path $Root 'PSXRECOMP_PIN') -Raw).Trim()
$Exe       = Join-Path $Root 'input/SLUS_002.24'

# Ninja inherits its target architecture from the active compiler environment.
# The generic Developer PowerShell shortcut can select x86, so relaunch through
# VsDevCmd when this is an active Visual Studio shell that is not fully x64.
$VisualStudioShell = $env:VSCMD_VER -or $env:VSINSTALLDIR
$VisualStudioX64 = ($env:VSCMD_ARG_HOST_ARCH -in @('x64', 'amd64')) -and
                   ($env:VSCMD_ARG_TGT_ARCH -in @('x64', 'amd64'))
if ($env:OS -eq 'Windows_NT' -and $VisualStudioShell -and !$VisualStudioX64) {
    if ($env:DISRUPTOR_X64_RELAUNCHED -eq '1') {
        throw 'Visual Studio x64 environment activation failed.'
    }

    $VsInstall = $env:VSINSTALLDIR
    $VsDevCmd = if ($VsInstall) {
        Join-Path $VsInstall 'Common7/Tools/VsDevCmd.bat'
    } else {
        $null
    }
    if (!$VsDevCmd -or !(Test-Path $VsDevCmd)) {
        $VsWhere = Join-Path ${env:ProgramFiles(x86)} `
            'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path $VsWhere) {
            $VsInstall = (& $VsWhere -latest -products '*' `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath | Select-Object -First 1)
            if ($VsInstall) {
                $VsDevCmd = Join-Path $VsInstall 'Common7/Tools/VsDevCmd.bat'
            }
        }
    }
    if (!$VsDevCmd -or !(Test-Path $VsDevCmd)) {
        throw 'Visual Studio VsDevCmd.bat was not found for the required x64 build.'
    }

    $PowerShellExe = (Get-Process -Id $PID).Path
    $env:DISRUPTOR_X64_RELAUNCHED = '1'
    $Relaunch = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 && "{1}" ' +
        '-NoProfile -ExecutionPolicy Bypass -File "{2}"'
    $Relaunch = $Relaunch -f $VsDevCmd, $PowerShellExe, $MyInvocation.MyCommand.Path
    Write-Host 'Re-launching the build with the Visual Studio x64 toolchain...'
    & $env:ComSpec /d /s /c $Relaunch
    $ExitCode = $LASTEXITCODE
    Remove-Item Env:DISRUPTOR_X64_RELAUNCHED -ErrorAction SilentlyContinue
    exit $ExitCode
}

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

function Get-CMakeFreshArguments {
    param([string]$BuildDirectory)

    $Cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if ($env:OS -ne 'Windows_NT' -or !(Test-Path $Cache)) {
        return
    }
    $Compiler = Get-Content $Cache |
        Where-Object { $_ -like 'CMAKE_C_COMPILER:FILEPATH=*' } |
        Select-Object -First 1
    if ($Compiler -match '(?i)Microsoft Visual Studio.*[\\/]cl\.exe$' -and
        $Compiler -notmatch '(?i)[\\/]HostX64[\\/]x64[\\/]cl\.exe$') {
        $VersionLine = (& cmake --version | Select-Object -First 1)
        if ($VersionLine -notmatch '^cmake version (\d+\.\d+\.\d+)') {
            throw "Could not determine the CMake version from: $VersionLine"
        }
        if ([version]$Matches[1] -lt [version]'3.24') {
            throw 'CMake 3.24 or newer is required to refresh a stale x86 cache.'
        }
        Write-Host "Refreshing non-x64 CMake cache: $BuildDirectory"
        return '--fresh'
    }
}

foreach ($Command in @('git', 'cmake', 'ctest', 'python')) {
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
& git -C $Framework cat-file -e "$Pin`^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) {
    Invoke-Checked { git -C $Framework fetch --quiet origin $Pin }
}
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
$RecompilerFresh = @(Get-CMakeFreshArguments $RecompilerBuild)
Invoke-Checked {
    cmake @RecompilerFresh -S (Join-Path $Framework 'recompiler') `
        -B $RecompilerBuild @Generator `
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
    New-Item -ItemType Directory -Force `
        (Join-Path $Framework 'generated') | Out-Null
    $BiosTool = Resolve-BuiltTool $RecompilerBuild 'psxrecomp-bios'
    Invoke-Checked { & $BiosTool --config bios/OpenBIOS.toml }
}
finally {
    Pop-Location
}

$Build = Join-Path $Root 'build'
$BuildFresh = @(Get-CMakeFreshArguments $Build)
Invoke-Checked {
    cmake @BuildFresh -S $Root -B $Build @Generator -DCMAKE_BUILD_TYPE=Release `
        -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
}
Invoke-Checked { cmake --build $Build --config Release --parallel }
Invoke-Checked { ctest --test-dir $Build -C Release --output-on-failure }

Write-Host ""
Write-Host "Build complete. Run .\run.ps1 after placing the matching BIN/CUE in input/."
