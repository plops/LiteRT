# LiteRT Build Instructions

The easiest way to build LiteRT from the source is using our docker image.
This document provides detailed build instructions for LiteRT across supported
platforms: Linux, macOS, Windows, Android.

## Installing Bazelisk (Recommended)

```bash
# Linux
curl -LO https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64
chmod +x bazelisk-linux-amd64
sudo mv bazelisk-linux-amd64 /usr/local/bin/bazel

# macOS (via Homebrew)
brew install bazelisk

# Windows (PowerShell)
Invoke-WebRequest -Uri https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-windows-amd64.exe -OutFile bazel.exe
Move-Item bazel.exe C:\Windows\System32\
```

## Setup local build environment

> [!TIP]
> You can use `./docker_build/hermetic_build.Dockerfile` as a reference for
> advanced environment setup including environment variable declaration and
> Android SDK / NDK installation.

## Platform-Specific Instructions

## Linux Build Instructions

### Step 1: Install Dependencies

```bash
# Update package manager
sudo apt-get update

# Install build essentials
sudo apt-get install -y \
    build-essential \
    curl \
    git \
    openjdk-17-jdk \
    python3 \
    python3-pip \
    python3-dev \
    unzip \
    wget \
    zip \
    llvm-18 \
    clang-18 \
    libc++-dev \
    libc++abi-dev
```

### Step 2: Clone and Build

#### Bazel Build (Linux)

```bash
git clone https://github.com/google-ai-edge/LiteRT.git
cd LiteRT
./configure

# Build
bazel build //litert/cc:litert_api_with_dynamic_runtime --repo_env=USE_HERMETIC_CC_TOOLCHAIN=0
bazel build //litert/tools:all --repo_env=USE_HERMETIC_CC_TOOLCHAIN=0
```

### Step 3: Verify Installation

```bash
# Test the benchmark tool
wget https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite
./bazel-bin/litert/tools/benchmark_model \
    --graph=blaze_face_short_range.tflite \
    --num_threads=4
```

Example output:

```
INFO: STARTING!
INFO: Log parameter values verbosely: [0]
INFO: Num threads: [4]
INFO: [litert/tools/benchmark_litert_model.cc:137] Loading model from: blaze_face_short_range.tflite
INFO: [litert/core/environment.cc:29] Creating LiteRT environment with options
WARNING: [litert/runtime/accelerators/auto_registration.cc:46] NPU accelerator could not be loaded and registered: kLiteRtStatusErrorInvalidArgument.
WARNING: All log messages before absl::InitializeLog() is called are written to STDERR
I0000 00:00:1758455104.504065   62590 accelerator_registry.cc:51] RegisterAccelerator: ptr=0x559f093a7dd0, name=CpuAccelerator
INFO: [litert/runtime/accelerators/auto_registration.cc:109] CPU accelerator registered.
INFO: [litert/runtime/compiled_model.cc:229] Applying compiler plugins...
INFO: [litert/runtime/compiled_model.cc:258] Flatbuffer model initialized directly from incoming litert model.
INFO: Created TensorFlow Lite XNNPACK delegate for CPU.
INFO: The input model file size (MB): 0.229746
INFO: Initialized session in 5.757ms.
INFO: Running benchmark for at least 1 iterations and at least 0.5 seconds but terminate if exceeding 150 seconds.
INFO: count=526 first=2125 curr=969 min=910 max=2125 avg=949.861 std=68 p5=915 median=939 p95=990

INFO: Running benchmark for at least 50 iterations and at least 1 seconds but terminate if exceeding 150 seconds.
INFO: count=1054 first=1015 curr=942 min=910 max=1102 avg=947.132 std=23 p5=915 median=942 p95=991

INFO: [./litert/tools/benchmark_litert_model.h:72] 
========== BENCHMARK RESULTS ==========
INFO: [./litert/tools/benchmark_litert_model.h:73] Model initialization: 5.76 ms
INFO: [./litert/tools/benchmark_litert_model.h:75] Warmup (first):       2.12 ms
INFO: [./litert/tools/benchmark_litert_model.h:77] Warmup (avg):         0.95 ms (526 runs)
INFO: [./litert/tools/benchmark_litert_model.h:79] Inference (avg):      0.95 ms (1054 runs)
INFO: [./litert/tools/benchmark_litert_model.h:83] Inference (min):      0.91 ms
INFO: [./litert/tools/benchmark_litert_model.h:85] Inference (max):      1.10 ms
INFO: [./litert/tools/benchmark_litert_model.h:87] Inference (std):      0.02
INFO: [./litert/tools/benchmark_litert_model.h:94] Throughput:           197.97 MB/s
INFO: [./litert/tools/benchmark_litert_model.h:103] 
Memory Usage:
INFO: [./litert/tools/benchmark_litert_model.h:105] Init footprint:       4.06 MB
INFO: [./litert/tools/benchmark_litert_model.h:107] Overall footprint:    7.07 MB
INFO: [./litert/tools/benchmark_litert_model.h:114] Peak memory usage not available. (peak_mem_mb <= 0)
INFO: [./litert/tools/benchmark_litert_model.h:117] ======================================

I0000 00:00:1758455106.022223   62590 accelerator_registry.cc:40] DestroyAccelerator: ptr=0x559f093a7dd0, name=CpuAccelerator
```

## macOS Build Instructions

### Step 1: Install Prerequisites

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install required packages
brew install python@3.11
```

### Step 2: Build for macOS

#### Bazel Build (macOS)

```bash
git clone https://github.com/google-ai-edge/LiteRT.git
cd LiteRT

# Build for Apple Silicon
bazel build --config=macos_arm64 //litert/cc:litert_api

# Or build for Intel Mac
bazel build --config=macos_x86_64 //litert/cc:litert_api

# Build tools
bazel build //litert/tools:all
```

## Windows Build Instructions

### Step 1: Install Prerequisites

```powershell
# Install Chocolatey
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install required packages
choco install git python3 -y

# Install Visual Studio Build Tools (if not installed)
choco install visualstudio2022buildtools -y
choco install visualstudio2022-workload-vctools -y
```

### Step 2: Configure Environment

```powershell
# Set environment variables
[System.Environment]::SetEnvironmentVariable("BAZEL_VC", "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC", "User")
[System.Environment]::SetEnvironmentVariable("BAZEL_SH", "C:\Program Files\Git\bin\bash.exe", "User")

# Restart PowerShell to apply changes
```

### Step 3: Build for Windows

#### Bazel Build (Windows)

```powershell
git clone https://github.com/google-ai-edge/LiteRT.git
cd LiteRT

# Configure for Windows
@"
build --config=windows
"@ | Out-File -Encoding ASCII .bazelrc.user

# Build
bazel build //litert/cc:litert_api
bazel build //litert/tools:all
```

## Android Build Instructions

### System Requirements

- **Host OS**: Linux, macOS, or Windows
- **Android SDK**: API level 21+ (Android 5.0+)
- **Android NDK**: r21+ recommended
- **Android Studio**: For sample apps

### Step 1: Install Android Development Tools

```bash
# Download Android SDK Command Line Tools
mkdir -p ~/android-sdk
cd ~/android-sdk
wget https://dl.google.com/android/repository/commandlinetools-linux-9477386_latest.zip
unzip commandlinetools-linux-9477386_latest.zip

# Set up environment
export ANDROID_HOME=~/android-sdk
export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$PATH
export PATH=$ANDROID_HOME/platform-tools:$PATH

# Install required SDK components
sdkmanager "platform-tools" "platforms;android-33" "build-tools;33.0.0" "ndk;21.4.7075529"
```

### Step 2: Build for Android with Bazel

```bash
cd LiteRT

# Configure for Android
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/21.4.7075529

# Build for ARM64
bazel build --config=android_arm64 \
  //litert/some_target
```

### Step 3: Test Accelerator Support for Android

```bash

# Push NPU libraries (Use Qualcomm as an example)
case "$PHONE" in
    's24')
        QNN_STUB_LIB="libQnnHtpV75Stub.so"
        QNN_SKEL_LIB="libQnnHtpV75Skel.so"
        QNN_SKEL_PATH_ARCH="hexagon-v75"
        ;;
    's25')
        QNN_STUB_LIB="libQnnHtpV79Stub.so"
        QNN_SKEL_LIB="libQnnHtpV79Skel.so"
        QNN_SKEL_PATH_ARCH="hexagon-v79"
        ;;
    ;; ...
esac

adb push "${HOST_NPU_LIB}/aarch64-android/libQnnHtp.so" "${DEVICE_NPU_LIBRARY_DIR}/"
adb push "${HOST_NPU_LIB}/aarch64-android/${QNN_STUB_LIB}" "${DEVICE_NPU_LIBRARY_DIR}/"
adb push "${HOST_NPU_LIB}/aarch64-android/libQnnSystem.so" "${DEVICE_NPU_LIBRARY_DIR}/"
adb push "${HOST_NPU_LIB}/aarch64-android/libQnnHtpPrepare.so" "${DEVICE_NPU_LIBRARY_DIR}/"
adb push "${HOST_NPU_LIB}/${QNN_SKEL_PATH_ARCH}/unsigned/${QNN_SKEL_LIB}" "${DEVICE_NPU_LIBRARY_DIR}/"

# Push GPU libraries
adb push "${HOST_GPU_LIBRARY_DIR}/libLiteRtGpuAccelerator.so" "${DEVICE_BASE_DIR}/"

# For Qualcomm NPU (ADSP_LIBRARY_PATH needs to be specified)
FULL_COMMAND="cd ${DEVICE_BASE_DIR} && LD_LIBRARY_PATH=\"${LD_LIBRARY_PATH}\" ADSP_LIBRARY_PATH=\"${ADSP_LIBRARY_PATH}\" ${RUN_COMMAND}"

# Other cases
FULL_COMMAND="cd ${DEVICE_BASE_DIR} && LD_LIBRARY_PATH=\"${LD_LIBRARY_PATH}\" ${RUN_COMMAND}"
```

## License

Copyright 2025 Google LLC. Licensed under Apache License 2.0.
