$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$generatedPreset = Join-Path $root "ConanPresets.json"
if (Test-Path -LiteralPath $generatedPreset) {
    Remove-Item -LiteralPath $generatedPreset -Force
}

$profiles = @(
    "windows-msvc-debug",
    "windows-msvc-release",
    "windows-clangcl-debug",
    "windows-clangcl-release"
)

foreach ($profile in $profiles) {
    $profilePath = "profiles/$profile"
    Write-Host "==> conan install $profile"
    conan install . --profile:host=$profilePath --profile:build=$profilePath --build=missing
}

Write-Host ""
Write-Host "Conan toolchains are ready for Visual Studio CMake presets."
