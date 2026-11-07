# Сборка APK

## Почему нельзя собрать прямо на телефоне

Этот проект — обычное Android-приложение с нативной частью (C++/NDK).
Официальные инструменты, которые вызывает Gradle, — `aapt2`
(build-tools) и компиляторы из NDK — поставляются Google только под
хост-платформу **linux-x86_64** (и под Windows/macOS x86_64).

Устройство, на котором лежат эти исходники, — **arm64-v8a (aarch64)**,
без эмуляции x86_64 (box64/qemu отсутствуют). Поэтому `aapt2` и
NDK-компиляторы на нём не запускаются, и `./gradlew assembleDebug`
принципиально не может отработать на телефоне, независимо от скорости
интернета.

Собирать нужно на x86_64-машине или в CI. Ниже два рабочих пути.

---

## Вариант A. GitHub Actions (проще всего)

В репозитории уже есть `.github/workflows/android.yml`, который сам
ставит JDK 21, SDK, NDK r28c и собирает debug APK.

1. Запушьте исходники в свой репозиторий (`vladislavluv6-websim/Test`)
   или загрузите их через веб-интерфейс.
2. Откройте **Actions → Android CI → Run workflow** (workflow_dispatch)
   либо сделайте push в любую ветку.
3. Дождитесь завершения job `build`.
4. Скачайте артефакт **`LeviLauncher-debug-apk`** — внутри готовый
   `app-debug.apk`.

Подписи/секреты для debug-сборки не нужны: workflow сам подставляет
placeholder `google-services.json` и собирает с
`-PdisableCrashlyticsUpload`.

---

## Вариант B. Локальная сборка на ПК (Linux x86_64 / macOS / WSL)

### 1. Поставить JDK 17+ (рекомендуется 21)

### 2. Поставить Android SDK

Через Android Studio (SDK Manager) или `sdkmanager` установите:

```
platform-tools
platforms;android-36
build-tools;35.0.0
ndk;28.2.13676358
```

Примите лицензии:

```
yes | sdkmanager --licenses
```

### 3. Указать SDK

Либо переменной окружения:

```
export ANDROID_HOME=/path/to/Android/Sdk
```

Либо файлом `local.properties` в корне проекта:

```
sdk.dir=/path/to/Android/Sdk
```

### 4. Собрать

```
chmod +x ./gradlew scripts/build_debug_apk.sh
./scripts/build_debug_apk.sh            # debug APK
# или
./scripts/build_debug_apk.sh assembleRelease
```

Скрипт сам создаст `local.properties` и placeholder
`app/google-services.json`, если их нет, и вызовет Gradle с
`-PdisableCrashlyticsUpload`.

Готовый файл появится в
`app/build/outputs/apk/debug/app-debug.apk`.

Если нужен «чистый» вызов Gradle напрямую:

```
./gradlew :app:assembleDebug -PdisableCrashlyticsUpload
```

---

## Submodules

`app/src/main/cpp/preloader` и `app/src/main/cpp/libHttpClient` — это
git-submodules. В архиве они уже с содержимым. Если делаете свежий
`git clone`, инициализируйте их:

```
git submodule update --init --recursive
```

## Примечания

* `google-services.json` и `local.properties` в `.gitignore` — их не
  нужно коммитить.
* Для реального Firebase (Crashlytics/Analytics) замените placeholder
  `app/google-services.json` на настоящий и уберите
  `-PdisableCrashlyticsUpload`.
* Проверено: изменённый нативный код (`AimAssist.cpp`, `ClientBridge.cpp`,
  `Esp.cpp`, `aimassist.cpp`) проходит `clang++ -std=c++20 -fsyntax-only`
  без ошибок.
