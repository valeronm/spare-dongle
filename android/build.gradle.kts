buildscript {
    repositories {
        mavenCentral()
    }
    dependencies {
        // The Kotlin Gradle plugin the Android Gradle plugin depends on calls a Gradle API deprecated in Gradle 9.
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:2.4.21")
    }
}

plugins {
    id("com.android.application") version "9.5.3" apply false
}
