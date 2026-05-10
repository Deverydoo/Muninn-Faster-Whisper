Set-Location "D:\Vibe_Projects\Muninn-Faster-Whisper\third_party\CTranslate2\build"

# Configure with all supported GPU architectures
# sm_60 = Pascal server (P100)
# sm_61 = Pascal consumer (GTX 10xx)
# sm_70 = Volta (V100, Titan V)
# sm_75 = Turing (RTX 20xx)
# sm_80 = Ampere datacenter (A100)
# sm_86 = Ampere consumer (RTX 30xx)
# sm_89 = Ada Lovelace (RTX 40xx)
# NOTE: sm_50/52 (Maxwell) excluded - no FP16 support

cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DWITH_CUDA=ON `
    -DWITH_CUDNN=ON `
    -DWITH_MKL=OFF `
    -DOPENMP_RUNTIME=NONE `
    -DCUDA_ARCH_LIST="6.0;6.1;7.0;7.5;8.0;8.6;8.9" `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_SHARED_LIBS=ON `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

Write-Host "Configuration complete. Building..."

cmake --build . --config Release --parallel

Write-Host "Build complete!"
