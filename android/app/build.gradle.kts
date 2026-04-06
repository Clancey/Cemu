plugins {
    id("com.android.application")
}

android {
    namespace = "info.cemu.cemu"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "info.cemu.cemu"
        minSdk = 29  // Meta Quest minimum
        targetSdk = 34
        versionCode = 1
        versionName = "2.0.0-android"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            abiFilters.addAll(listOf("arm64-v8a"))
        }

        externalNativeBuild {
            cmake {
                val vcpkgRoot = file("${projectDir}/../../dependencies/vcpkg").absolutePath
                val ndkDir = android.ndkDirectory.absolutePath
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_PLATFORM=android-29",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DVCPKG_TARGET_TRIPLET=arm64-android",
                    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${ndkDir}/build/cmake/android.toolchain.cmake",
                    "-DCMAKE_TOOLCHAIN_FILE=${vcpkgRoot}/scripts/buildsystems/vcpkg.cmake"
                )
                cppFlags += listOf("-std=c++20", "-fexceptions", "-frtti")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
}