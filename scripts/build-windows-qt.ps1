param(
    [string]$DriveLetter = "T",
    [string]$QtRoot,
    [string]$BuildToolRoot,
    [string]$QtVersion = "6.10.3",
    [string]$BuildDir = "build-win-qt-w64",
    [string]$BuildType = "Release",
    [int]$Jobs = 1
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$workspaceRoot = Resolve-Path -LiteralPath (Join-Path $repoRoot "..")

if ([string]::IsNullOrWhiteSpace($BuildToolRoot)) {
    $BuildToolRoot = Join-Path $workspaceRoot "tools\build"
}
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $env:USERPROFILE "codex-tools\qt"
}

$buildToolRootResolved = Resolve-Path -LiteralPath $BuildToolRoot
$qtRootResolved = Resolve-Path -LiteralPath $QtRoot
$workspaceBuildToolRoot = Resolve-Path -LiteralPath (Join-Path $workspaceRoot "tools\build")
$useSubstBuildTools = [string]::Equals(
    $buildToolRootResolved.Path,
    $workspaceBuildToolRoot.Path,
    [System.StringComparison]::OrdinalIgnoreCase)

$cmakeBin = Join-Path $buildToolRootResolved "cmake-4.3.3-windows-x86_64\bin"
$w64Bin = Join-Path $buildToolRootResolved "w64devkit\bin"
$qtPrefix = Join-Path $qtRootResolved "$QtVersion\mingw_64"
$qtBin = Join-Path $qtPrefix "bin"

$cmakeExe = Join-Path $cmakeBin "cmake.exe"
$makeExe = Join-Path $w64Bin "mingw32-make.exe"
$cxxExe = Join-Path $w64Bin "c++.exe"

if (-not (Test-Path -LiteralPath $cmakeExe)) {
    throw "CMake not found: $cmakeExe"
}
if (-not (Test-Path -LiteralPath $makeExe)) {
    throw "w64devkit make not found: $makeExe"
}
if (-not (Test-Path -LiteralPath $cxxExe)) {
    throw "w64devkit C++ compiler not found: $cxxExe"
}
if (-not (Test-Path -LiteralPath (Join-Path $qtPrefix "lib\cmake\Qt6\Qt6Config.cmake"))) {
    throw "Qt $QtVersion was not found under $qtRootResolved"
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
    if ($useSubstBuildTools) {
        $buildToolRootForBuild = "$drive\tools\build"
    } else {
        $buildToolRootForBuild = $buildToolRootResolved.Path
    }

    $cmakeBinForBuild = Join-Path $buildToolRootForBuild "cmake-4.3.3-windows-x86_64\bin"
    $w64BinForBuild = Join-Path $buildToolRootForBuild "w64devkit\bin"
    $makeExeForBuild = Join-Path $w64BinForBuild "mingw32-make.exe"

    $env:Path = "$cmakeBinForBuild;$w64BinForBuild;$qtBin;$env:Path"
    Push-Location "$drive\thor-load-scope"
    try {
        cmake -S . -B $BuildDir -G "MinGW Makefiles" `
            -DCMAKE_BUILD_TYPE=$BuildType `
            -DTLSCOPE_BUILD_QT=ON `
            -DCMAKE_PREFIX_PATH="$qtPrefix" `
            -DCMAKE_MAKE_PROGRAM="$makeExeForBuild"
        cmake --build $BuildDir -j $Jobs
    } finally {
        Pop-Location
    }
} finally {
    & $subst $drive /D 2>$null | Out-Null
}
