$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Tool = Join-Path $Root 'psxrecomp/recompiler/build/psxrecomp-game.exe'
$Exe  = Join-Path $Root 'input/SLUS_002.24'

if (!(Test-Path $Tool)) {
    throw "PSXRecomp has not been built. Run .\build.ps1 first."
}
if (!(Test-Path $Exe)) {
    throw "Missing input/SLUS_002.24. Extract it from your SLUS-00224 disc."
}

Push-Location $Root
try {
    python tools/inspect_exe.py
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    python tools/extract_code_image.py
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    python tools/generate_seeds.py
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $Tool --config game.toml
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    python tools/audit_codegen.py
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
