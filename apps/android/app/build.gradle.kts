plugins {
    id("com.android.application")
}

val buildMelodyNative by tasks.registering(Exec::class) {
    workingDir = rootProject.projectDir.parentFile.parentFile
    commandLine("./nob", "android")
}

android {
    namespace = "orgwall.melody"
    compileSdk = 36

    defaultConfig {
        applicationId = "orgwall.melody"
        minSdk = 23
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

tasks.named("preBuild") {
    dependsOn(buildMelodyNative)
}
