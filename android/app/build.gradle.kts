import java.util.Properties

plugins {
    id("com.android.application")
}

// Release signing: android/keystore.properties (NOT in git) points at the upload keystore.
//   storeFile=keystore/nostrings-upload.jks
//   storePassword=...
//   keyAlias=nostrings
//   keyPassword=...
// Without it the release build signs with the debug key (installable, but not for Play).
val keystoreProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "com.blinkzero.nostrings"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.blinkzero.nostrings"
        minSdk = 26            // MidiManager (23) + a WebView new enough for AudioWorklet
        targetSdk = 36
        versionCode = 1        // bump for every Play upload
        versionName = "1.0"
    }

    signingConfigs {
        if (keystoreProps.containsKey("storeFile")) {
            create("release") {
                storeFile = rootProject.file(keystoreProps.getProperty("storeFile"))
                storePassword = keystoreProps.getProperty("storePassword")
                keyAlias = keystoreProps.getProperty("keyAlias")
                keyPassword = keystoreProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = if (keystoreProps.containsKey("storeFile")) signingConfigs.getByName("release")
                            else signingConfigs.getByName("debug")
        }
    }

    bundle {
        language { enableSplit = false }   // one asset, one language: nothing to split
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8   // plain Java 8 in MainActivity; also keeps AGP from needing jlink
        targetCompatibility = JavaVersion.VERSION_1_8
    }
}

// The web build is the app: ../../web/index.html (made by vst/webhost/build-web.ps1) is copied
// into src/main/assets before every build. That folder is generated and ignored by git.
val copyWebBuild by tasks.registering(Copy::class) {
    from(rootProject.file("../web/index.html"))
    into(layout.projectDirectory.dir("src/main/assets"))
}
tasks.named("preBuild") { dependsOn(copyWebBuild) }

dependencies {
    implementation("androidx.webkit:webkit:1.12.1")
}
