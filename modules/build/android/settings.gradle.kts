pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
    plugins {
        id("com.android.application") version "8.13.2"
        id("com.android.library") version "8.13.2"
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = providers.gradleProperty("melody.rootProjectName").orNull ?: "melody-app"
include(":app")
(providers.gradleProperty("melody.libraryProjects").orNull ?: "")
    .split(",").filter { it.isNotEmpty() }.forEach { include(it) }
