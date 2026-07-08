param(
    [string]$DriveLetter = "T"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$workspaceRoot = Resolve-Path -LiteralPath (Join-Path $repoRoot "..")
$toolRoot = Join-Path $workspaceRoot "tools\build"
$cmakeBin = Join-Path $toolRoot "cmake-4.3.3-windows-x86_64\bin"
$w64Bin = Join-Path $toolRoot "w64devkit\bin"
$cmakeExe = Join-Path $cmakeBin "cmake.exe"
$makeExe = Join-Path $w64Bin "mingw32-make.exe"

if (-not (Test-Path -LiteralPath $cmakeExe)) {
    throw "CMake not found: $cmakeExe"
}
if (-not (Test-Path -LiteralPath $makeExe)) {
    throw "w64devkit make not found: $makeExe"
}

$drive = $DriveLetter.TrimEnd(":") + ":"
$subst = Join-Path $env:SystemRoot "System32\subst.exe"

try {
    & $subst $drive /D 2>$null | Out-Null
} catch {
    # The drive may not exist yet.
}

& $subst $drive $workspaceRoot.Path

try {
    $env:Path = "$drive\tools\build\cmake-4.3.3-windows-x86_64\bin;$drive\tools\build\w64devkit\bin;$env:Path"
    Push-Location "$drive\thor-load-scope"
    try {
        cmake -S . -B build-win -G "MinGW Makefiles" `
            -DCMAKE_BUILD_TYPE=Release `
            -DTLSCOPE_BUILD_QT=OFF `
            -DCMAKE_MAKE_PROGRAM="$drive\tools\build\w64devkit\bin\mingw32-make.exe"
        cmake --build build-win -j 4
    } finally {
        Pop-Location
    }
} finally {
    & $subst $drive /D 2>$null | Out-Null
}
