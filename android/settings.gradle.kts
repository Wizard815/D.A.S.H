pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "dash"

// core: the pure Kotlin parts (frames, parsing, signing, HTTP clients). It
// builds and tests on any JVM, with no Android SDK.
include(":core")

// app: the Android APK. Needs the Android SDK. `-PcoreOnly` leaves it out,
// for running the core tests on a machine without the SDK.
if (!providers.gradleProperty("coreOnly").isPresent) {
    include(":app")
}
