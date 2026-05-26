plugins {
    id("com.android.application")
}

val mel = providers
fun prop(name: String): String = mel.gradleProperty(name).get()
fun propOrNull(name: String): String? = mel.gradleProperty(name).orNull
fun csv(name: String): List<File> =
    (propOrNull(name) ?: "").split(",").filter { it.isNotEmpty() }.map { File(it) }

android {
    namespace = prop("melody.namespace")
    compileSdk = prop("melody.compileSdk").toInt()

    defaultConfig {
        applicationId = prop("melody.applicationId")
        minSdk = prop("melody.minSdk").toInt()
        targetSdk = prop("melody.targetSdk").toInt()
        versionCode = prop("melody.versionCode").toInt()
        versionName = prop("melody.versionName")
        manifestPlaceholders["appLabel"] = prop("melody.appLabel")
    }

    flavorDimensions += "dist"
    productFlavors {
        create("melody") { dimension = "dist" }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs(*csv("melody.javaSrcDirs").toTypedArray())
            jniLibs.srcDirs("src/main/jniLibs")
        }
        getByName("melody") {
            propOrNull("melody.manifestOverlay")?.let { manifest.srcFile(File(it)) }
            propOrNull("melody.appJavaDir")?.let { java.srcDir(File(it)) }
            propOrNull("melody.resOverlayDir")?.let { res.srcDir(File(it)) }
        }
    }
}
