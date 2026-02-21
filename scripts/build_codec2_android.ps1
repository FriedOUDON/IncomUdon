param(
    [string]$NdkPath = $env:ANDROID_NDK_HOME,
    [string]$Api = "24",
    [string[]]$Abis = @("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
)

if (-not $NdkPath) {
    Write-Error "Set ANDROID_NDK_HOME or pass -NdkPath"
    exit 1
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$src = Join-Path $root "third_party/libcodec2"
$installRoot = Join-Path $root "third_party/libcodec2_android"
$buildRoot = Join-Path $installRoot "build"

foreach ($abi in $Abis) {
    Write-Host "Building codec2 for $abi"
    $buildDir = Join-Path $buildRoot $abi

    & cmake -S $src -B $buildDir `
        -DCMAKE_TOOLCHAIN_FILE="$NdkPath/build/cmake/android.toolchain.cmake" `
        -DANDROID_ABI=$abi `
        -DANDROID_PLATFORM=android-$Api `
        -DBUILD_SHARED_LIBS=ON `
        -DUNITTEST=OFF `
        -DINSTALL_EXAMPLES=OFF `
        -DLPCNET=OFF `
        -DCMAKE_BUILD_TYPE=Release

    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & cmake --build $buildDir --config Release --target codec2
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & cmake --install $buildDir --prefix (Join-Path $installRoot $abi)
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
