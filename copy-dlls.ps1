$ErrorActionPreference = "Stop"

$source_dll = "third_party\SDL3\build\Debug\SDL3.dll"

if (-Not (Test-Path $source_dll)) {
    exit 1
}

$targets = @(
    "build\src\Debug\"
)

foreach ($target in $targets) {
    if (-Not (Test-Path $target)) {
        mkdir $target -Force | Out-Null
    }
    
    Copy-Item $source_dll "$target\SDL3.dll" -Force
}