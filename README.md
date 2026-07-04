# ViPERFX_RE

This is a reverse-engineering project aimed to make ViPER4Android more easy to access, modify, and improve.
Some QOL changes have been made, such as processing audio as float32, removing unused code, using more modern libraries, etc.
To view the progress, see [Issue #2](https://github.com/AndroidAudioMods/ViPERFX_RE/issues/2)

# Building

- Install the NDK, CMake and Make.
- Run `cmake . -DCMAKE_TOOLCHAIN_FILE=<NDKROOT>/build/cmake/android.toolchain.cmake -DANDROID_ABI=<ABI>` where <NDKROOT> is where you installed the NDK and <ABI> is the target ABI to build for.
- Run `make`
- You should now find a `libv4afx_r.so` file in your project root.

# CLion

Make sure to install `ninja`
In CLion, make sure to set the following custom arguments: `-DCMAKE_TOOLCHAIN_FILE=<NDKROOT>/build/cmake/android.toolchain.cmake -DANDROID_ABI=<ABI> -G Ninja`
Then you can build the project with `ninja` instead of `make`

# Credits

Zhuhang and ViPER520 for making ViPER4Android
Martmists and Iscle for reverse-engineering

# AIDL2 Bug Fixes (Fork)

This fork contains essential bug fixes for the ViPERFX_RE AIDL2 backend:
- **Stereo Channel Collapse & Muting Fix**: Fixed a critical bug where the right audio channel was zeroed out (muted) when ViPER was enabled. This was caused by the AIDL interface parsing the 32-bit `PARAM_SET_OUTPUT_GAIN` (Master Volume) as two separate 8-bit bytes (L/R) and reading uninitialized memory for the right channel. Master volume and panning are now correctly decoupled and implemented.
- **Output Panning Implementation**: Implemented the missing `PARAM_SET_OUTPUT_PAN` command to support the channel panning slider in the ViPER app.
- **Limiter Epsilon Guard**: Added a protection check in `SoftwareLimiter.cpp` to prevent undefined behavior and potential division-by-zero math errors when completely silent audio streams were processed.
- **Memory Safety Fixes**: Fixed an overlapping `memcpy` call in `ViPERContext::process` that was corrupting stereo frames during AIDL in-place processing.
- **Diagnostic Polling Parameters**: Fixed missing `PARAM_GET_STREAMING`, `PARAM_GET_SAMPLING_RATE`, and `PARAM_GET_CONVOLUTION_KERNEL_ID` AIDL commands to allow the frontend app to correctly display the driver status, sample rate, and active processing state.
