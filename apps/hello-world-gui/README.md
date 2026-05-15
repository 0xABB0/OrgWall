# Hello World GUI

Cross-platform melody application. The app code in `src/hello_world_gui.c` only calls melody APIs (`gui`, `gui.control`, `gui.app`). Platform deployment scaffolding lives in subdirectories alongside.

## Android

```sh
cp android/local.properties.example android/local.properties
# Edit android/local.properties so sdk.dir points at your Android SDK.

./nob build app hello-world-gui android    # build APK
./nob run   app hello-world-gui android    # build + install + launch
./nob debug app hello-world-gui android    # run + stream Melody logcat
```

`nob` cross-compiles `libmelody.so` for the four Android ABIs into `android/app/src/main/jniLibs/<abi>/`, then drives gradle to assemble the APK. Don't invoke gradle directly — go through `./nob`.

The Android-specific Java glue (`MelodyActivity`, `NativeGuiHost`) lives in `modules/gui.platform.android/src/android/java/` and is pulled in via a gradle source set, not duplicated here.
