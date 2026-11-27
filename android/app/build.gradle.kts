plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "io.github.valeronm.sparedongle"
    compileSdk {
        version = release(37) { minorApiLevel = 1 }
    }
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "io.github.valeronm.sparedongle"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        ndk.abiFilters += "arm64-v8a"
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    testOptions {
        unitTests.all {
            mapOf(
                "sparedongle.strings" to layout.projectDirectory.file("src/main/res/values/strings.xml").asFile,
                "sparedongle.filter" to layout.projectDirectory.file("src/main/res/xml/dongle.xml").asFile,
                "sparedongle.codes" to rootProject.layout.projectDirectory.file("codes.json").asFile,
            ).forEach { (name, file) ->
                it.inputs.file(file)
                it.systemProperty(name, file.path)
            }
        }
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.11.01"))
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.annotation:annotation:1.10.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20250517")
}

val ndkDirectory = androidComponents.sdkComponents.ndkDirectory

abstract class CargoNdk : Exec() {
    @get:OutputDirectory
    abstract val jniLibs: DirectoryProperty
}

androidComponents.onVariants { variant ->
    val cargoNdk = tasks.register<CargoNdk>("cargoNdk${variant.name.replaceFirstChar(Char::uppercase)}") {
        val workspace = rootDir.parentFile
        workingDir = workspace
        doFirst { environment("ANDROID_NDK_HOME", ndkDirectory.get().asFile.absolutePath) }
        commandLine("cargo", "ndk", "-t", "arm64-v8a", "-o")
        argumentProviders.add { listOf(jniLibs.get().asFile.path, "build", "--release", "-p", "diagjni") }
        for (crate in listOf("android/rust", "client", "wire")) {
            inputs.dir(workspace.resolve("$crate/src"))
            inputs.file(workspace.resolve("$crate/Cargo.toml"))
        }
        inputs.files(workspace.resolve("Cargo.toml"), workspace.resolve("Cargo.lock"))
        inputs.property("ndk", android.ndkVersion)
        for ((name, tool) in listOf("rustc" to listOf("rustc"), "cargoNdk" to listOf("cargo", "ndk"))) {
            inputs.property(name, providers.exec {
                workingDir = workspace
                commandLine(tool + "--version")
            }.standardOutput.asText)
        }
    }
    checkNotNull(variant.sources.jniLibs) { "${variant.name} has no jniLibs sources for the native library" }
        .addGeneratedSourceDirectory(cargoNdk, CargoNdk::jniLibs)
}
