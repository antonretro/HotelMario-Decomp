# Build the Hotel Mario Android APK without Gradle (Gradle's Android plugin fails on this PC's NDK 30).
# Needs: Android SDK build-tools 34, platform android-34, Android Studio's JDK (JBR), 7-Zip, and the native
# library build/android/app/libs/armeabi-v7a/libmain.so + libSDL2.so from ndk-build.
# Output: build/android/HotelMario.apk (debug-signed; game data is NOT in it - push it with adb).

$ErrorActionPreference = "Stop"
$root    = Resolve-Path (Join-Path $PSScriptRoot "..")
$and     = Join-Path $root "build\android"
$sdk     = "$env:LOCALAPPDATA\Android\Sdk"
$bt      = Join-Path $sdk "build-tools\34.0.0"
$jar     = Join-Path $sdk "platforms\android-34\android.jar"
$jdk     = "C:\Program Files\Android\Android Studio\jbr\bin"
$sevenz  = "C:\Program Files\7-Zip\7z.exe"
$out     = Join-Path $and "apk"
$res     = Join-Path $and "app\src\main\res"
$man     = Join-Path $and "app\src\main\AndroidManifest.xml"
$javaSrc = Join-Path $and "app\src\main\java"
$libs    = Join-Path $and "app\libs\armeabi-v7a"

Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$out\gen", "$out\classes", "$out\dex" | Out-Null

# 1. resources (the manifest gets its package name here; Gradle would normally set it from the namespace)
$manText = [IO.File]::ReadAllText($man)
$manText = $manText -replace '<manifest\s', '<manifest package="org.libsdl.app" '
$manText = $manText -replace 'android:label="@string/app_name"', 'android:label="Hotel Mario"'
[IO.File]::WriteAllText("$out\AndroidManifest.xml", $manText)
& "$bt\aapt2.exe" compile --dir $res -o "$out\res.zip"
if ($LASTEXITCODE) { throw "aapt2 compile failed" }
& "$bt\aapt2.exe" link -I $jar --manifest "$out\AndroidManifest.xml" --java "$out\gen" --auto-add-overlay -o "$out\base.apk" "$out\res.zip"
if ($LASTEXITCODE) { throw "aapt2 link failed" }

# 2. Java: SDL's activity + the generated R class, compiled against android.jar
$srcs = @(Get-ChildItem $javaSrc -Recurse -Filter *.java | ForEach-Object { $_.FullName })
$srcs += @(Get-ChildItem "$out\gen" -Recurse -Filter *.java | ForEach-Object { $_.FullName })
& "$jdk\javac.exe" --release 8 -nowarn -cp $jar -d "$out\classes" $srcs
if ($LASTEXITCODE) { throw "javac failed" }

# 3. dex
$classFiles = @(Get-ChildItem "$out\classes" -Recurse -Filter *.class | ForEach-Object { $_.FullName })
$classList = "$out\classes.txt"
$classFiles | Set-Content -Encoding ascii $classList
$bt36 = Join-Path $sdk "build-tools\36.1.0"
& "$jdk\java.exe" -cp "$bt36\lib\d8.jar" com.android.tools.r8.D8 --min-api 21 --lib $jar --output "$out\dex" @$classList
if ($LASTEXITCODE) { throw "d8 failed" }

# 4. package: dex + native libraries
Copy-Item "$out\base.apk" "$out\unsigned.apk"
Push-Location "$out\dex"; & $sevenz a -tzip "$out\unsigned.apk" classes.dex | Out-Null; Pop-Location
New-Item -ItemType Directory -Force "$out\pkg\lib\armeabi-v7a" | Out-Null
Copy-Item "$libs\*.so" "$out\pkg\lib\armeabi-v7a\"
Push-Location "$out\pkg"; & $sevenz a -tzip "$out\unsigned.apk" "lib" | Out-Null; Pop-Location

# 5. align and sign with a debug key
& "$bt\zipalign.exe" -f -p 4 "$out\unsigned.apk" "$out\aligned.apk"
$ks = Join-Path $out "debug.keystore"
if (-not (Test-Path $ks)) {
    & "$jdk\keytool.exe" -genkeypair -keystore $ks -storepass android -keypass android -alias androiddebugkey `
        -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA -keysize 2048 -validity 10000 | Out-Null
}
& "$bt\apksigner.bat" sign --ks $ks --ks-pass pass:android --ks-key-alias androiddebugkey --key-pass pass:android `
    --out (Join-Path $and "HotelMario.apk") "$out\aligned.apk"
if ($LASTEXITCODE) { throw "apksigner failed" }
Get-Item (Join-Path $and "HotelMario.apk") | Select-Object FullName, @{n='MB';e={[math]::Round($_.Length/1MB,1)}}
