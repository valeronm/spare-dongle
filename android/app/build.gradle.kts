plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.valeronm.sparedongle"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "io.github.valeronm.sparedongle"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        ndk.abiFilters += "arm64-v8a"
    }
}

val ndkDirectory = androidComponents.sdkComponents.ndkDirectory

val cargoNdk = tasks.register<Exec>("cargoNdk") {
    val workspace = rootDir.parentFile
    workingDir = workspace
    doFirst { environment("ANDROID_NDK_HOME", ndkDirectory.get().asFile.absolutePath) }
    commandLine("cargo", "ndk", "-t", "arm64-v8a", "-o", "android/app/src/main/jniLibs",
        "build", "--release", "-p", "diagjni")
    for (crate in listOf("android/rust", "client", "wire")) {
        inputs.dir(workspace.resolve("$crate/src"))
        inputs.file(workspace.resolve("$crate/Cargo.toml"))
    }
    inputs.files(workspace.resolve("Cargo.toml"), workspace.resolve("Cargo.lock"))
    outputs.dir(layout.projectDirectory.dir("src/main/jniLibs"))
}
tasks.named("preBuild") { dependsOn(cargoNdk) }
