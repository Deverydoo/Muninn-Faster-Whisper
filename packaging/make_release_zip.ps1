# Builds Muninn-<version>-win-x64-cuda13.zip from a CUDA Release build.
#
#   packaging/make_release_zip.ps1 -BuildDir build_release `
#       -FfmpegDir <ffmpeg install with bin/*.dll> `
#       -FfmpegSourceDir <ffmpeg-9.0.2 source tree, for COPYING.LGPLv2.1 and LICENSE.md> `
#       -ExtraLicensesDir <folder with dav1d-COPYING.txt, zlib-LICENSE.txt,
#                          nv-codec-headers-LICENSE.txt, silero-vad-LICENSE.txt>
#
# Writes the zip and its .sha256 to the repository root. CUDA and cuDNN are
# not bundled; README_BINARIES.txt lists them as requirements.
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $FfmpegDir,
    [Parameter(Mandatory)] [string] $FfmpegSourceDir,
    [Parameter(Mandatory)] [string] $ExtraLicensesDir,
    [string] $CT2Dir = "third_party/CTranslate2-4.9.1",
    [string] $OrtDir = "third_party/onnx/onnxruntime-win-x64-1.31.0",
    [string] $SpmDir = "third_party/sentencepiece",
    [string] $SileroModel = "models/silero_vad.onnx"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(MuninnFasterWhisper VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$name = "Muninn-$version-win-x64-cuda13"
$stage = Join-Path ([IO.Path]::GetTempPath()) "muninn-release/$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
foreach ($d in "bin", "lib", "include", "models", "licenses") {
    New-Item -ItemType Directory -Force (Join-Path $stage $d) | Out-Null
}

# The build's own output folder (Visual Studio adds the configuration).
$out = Join-Path $BuildDir "Release/Release"
if (-not (Test-Path "$out/muninn.dll")) { $out = Join-Path $BuildDir "Release" }

function Copy-Required([string] $from, [string] $to) {
    if (-not (Test-Path $from)) { throw "Missing: $from" }
    Copy-Item $from $to
}

# Binaries: muninn.dll and everything it loads that is not Windows, MSVC or CUDA.
Copy-Required "$out/muninn.dll"      "$stage/bin"
Copy-Required "$out/ctranslate2.dll" "$stage/bin"
Copy-Required "$out/onnxruntime.dll" "$stage/bin"
foreach ($dll in "avformat-63.dll", "avcodec-63.dll", "avutil-61.dll", "swresample-7.dll") {
    Copy-Required "$FfmpegDir/bin/$dll" "$stage/bin"
}
Copy-Required "$out/muninn.lib"         "$stage/lib"
Copy-Required "$OrtDir/lib/onnxruntime.lib" "$stage/lib"

# Headers
Copy-Item -Recurse "include/muninn" "$stage/include/muninn"
Copy-Item -Recurse "$OrtDir/include" "$stage/include/onnxruntime"

Copy-Required $SileroModel "$stage/models"

# Licenses
$lic = "$stage/licenses"
Copy-Required "$CT2Dir/LICENSE"                          "$lic/CTranslate2-LICENSE.txt"
Copy-Required "$CT2Dir/third_party/cpu_features/LICENSE" "$lic/cpu_features-LICENSE.txt"
Copy-Required "$CT2Dir/third_party/spdlog/LICENSE"       "$lic/spdlog-LICENSE.txt"
Copy-Required "$CT2Dir/third_party/thrust/LICENSE"       "$lic/thrust-LICENSE.txt"
Copy-Required "$CT2Dir/third_party/cutlass/LICENSE.txt"  "$lic/cutlass-LICENSE.txt"
Copy-Required "$CT2Dir/third_party/ruy/LICENSE"          "$lic/ruy-LICENSE.txt"
Copy-Required "$CT2Dir/third_party/ruy/third_party/cpuinfo/LICENSE" "$lic/cpuinfo-LICENSE.txt"
Copy-Required "$OrtDir/LICENSE"                          "$lic/onnxruntime-LICENSE.txt"
Copy-Required "$OrtDir/ThirdPartyNotices.txt"            "$lic/onnxruntime-ThirdPartyNotices.txt"
Copy-Required "$FfmpegSourceDir/COPYING.LGPLv2.1"        "$lic/LGPL-2.1.txt"
Copy-Required "$FfmpegSourceDir/LICENSE.md"              "$lic/FFmpeg-LICENSE.md"
Copy-Required "$SpmDir/LICENSE"                          "$lic/sentencepiece-LICENSE.txt"
foreach ($p in "absl", "protobuf-lite", "darts_clone", "esaxx") {
    Copy-Required "$SpmDir/third_party/$p/LICENSE" "$lic/sentencepiece-$p-LICENSE.txt"
}
foreach ($f in "dav1d-COPYING.txt", "zlib-LICENSE.txt", "nv-codec-headers-LICENSE.txt", "silero-vad-LICENSE.txt") {
    Copy-Required "$ExtraLicensesDir/$f" "$lic/$f"
}

Copy-Required "LICENSE" $stage
Copy-Required "packaging/THIRD_PARTY_NOTICES.txt" $stage
(Get-Content -Raw "packaging/README_BINARIES.txt").Replace("@VERSION@", $version) |
    Set-Content -NoNewline -Encoding utf8NoBOM "$stage/README_BINARIES.txt"

$zip = Join-Path $root "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content -Encoding ascii "$zip.sha256"
Write-Host "$zip"
Write-Host "SHA-256: $hash"
