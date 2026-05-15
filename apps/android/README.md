# Melody Android

Small Android hello world application for Melody.

## Build

Point Gradle at your Android SDK, then build the debug APK:

```sh
cp local.properties.example local.properties
# Edit local.properties so sdk.dir points at your Android SDK.
gradle :app:assembleDebug
```

On this machine, the SDK appears to live at:

```text
/Users/gabbo/Library/Android/sdk
```

Android Studio can also open this directory directly:

```text
apps/android
```
