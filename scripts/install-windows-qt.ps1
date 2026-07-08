param(
    [string]$AqtRoot,
    [string]$QtRoot,
    [string]$QtVersion = "6.10.3"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$workspaceRoot = Resolve-Path -LiteralPath (Join-Path $repoRoot "..")

if ([string]::IsNullOrWhiteSpace($AqtRoot)) {
    $AqtRoot = Join-Path $workspaceRoot "tools\aqtinstall"
}
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $env:USERPROFILE "codex-tools\qt"
}

New-Item -ItemType Directory -Force -Path $AqtRoot | Out-Null
New-Item -ItemType Directory -Force -Path $QtRoot | Out-Null

$aqtExe = Join-Path $AqtRoot "aqt.exe"
if (-not (Test-Path -LiteralPath $aqtExe)) {
    winget install --id miurahr.aqtinstall --exact `
        --accept-package-agreements `
        --accept-source-agreements `
        --location $AqtRoot
}

if (-not (Test-Path -LiteralPath $aqtExe)) {
    $aqtCommand = Get-Command aqt -ErrorAction SilentlyContinue
    if ($null -eq $aqtCommand) {
        throw "aqt.exe was not found after installing aqtinstall"
    }
    $aqtExe = $aqtCommand.Source
}

& $aqtExe install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O $QtRoot
& $aqtExe install-qt windows desktop $QtVersion win64_mingw --archives qtbase -O $QtRoot

Write-Host "Qt $QtVersion qtbase installed under $QtRoot"
