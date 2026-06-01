plugins {
    id("com.android.application")
}

val mel = providers
fun prop(name: String): String = mel.gradleProperty(name).get()
fun propOrNull(name: String): String? = mel.gradleProperty(name).orNull

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
            jniLibs.srcDirs("src/main/jniLibs")
            propOrNull("melody.appJava")?.let { java.srcDir(File(it)) }
        }
        getByName("melody") {
            propOrNull("melody.appManifest")?.let { manifest.srcFile(File(it)) }
        }
    }
}

dependencies {
    (propOrNull("melody.libraryProjects") ?: "").split(",").filter { it.isNotEmpty() }.forEach {
        implementation(project(it))
    }
}
