plugins {
    id("com.android.application")
}

val melodyRoot = rootProject.projectDir.parentFile.parentFile.parentFile

android {
    namespace = "orgwall.helloworld"
    compileSdk = 36

    defaultConfig {
        applicationId = "orgwall.helloworld"
        minSdk = 23
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs(
                "src/main/java",
                File(melodyRoot, "modules/gui/src/android/java"),
                File(melodyRoot, "modules/music.midi/src/android/java"),
            )
        }
    }
}
