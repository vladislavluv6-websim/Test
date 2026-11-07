# Что исправлено и доработано

## 1. Краш при открытии меню (главное)

Причина найдена точно, по байткоду библиотеки Material 1.13.0:

* `MinecraftActivity` объявлена в манифесте с темой `AppFullScreenTheme`
  (`Theme.AppCompat.NoActionBar`).
* Карточки модулей в меню (`item_mod_menu_card.xml` и
  `item_mod_menu_compact.xml`) были объявлены как
  `com.google.android.material.card.MaterialCardView`.
* `MaterialCardView` использует стиль `Widget.MaterialComponents.CardView`,
  у которого выставлен `enforceMaterialTheme=true`. В AppCompat-теме нет
  `isMaterialTheme`, поэтому конструктор бросает
  `IllegalArgumentException: The style on this component requires your app
  theme to be Theme.MaterialComponents (or a descendant)`.
* Это происходило уже после `windowManager.addView`, на layout-проходе
  RecyclerView, то есть **вне** `try/catch` метода `showInternal()` —
  поэтому приложение падало, а не открывало fallback.

Исправлено:

* `item_mod_menu_card.xml`, `item_mod_menu_compact.xml` переведены на
  `androidx.cardview.widget.CardView`; убраны Material-only атрибуты
  (`strokeColor`, `strokeWidth`, `rippleColor`), добавлен
  `android:foreground="?attr/selectableItemBackground"`. Внешний вид и
  обработка `setCardBackgroundColor()`/`setCardElevation()` сохранены.
* `ModMenuAdapter` дополнительно инфлейтит карточки через
  `ContextThemeWrapper(Theme.MaterialComponents.NoActionBar)`. Даже если
  позже в item-layout снова добавят Material-виджет, layout-проход не
  уронит игру.
* `ModMenuOverlay.showInternal()` ловит `Throwable`, логирует
  (`Log.e("ModMenuOverlay", ...)`), а `showFallback()` обёрнут в
  собственный try/catch. Ошибка открытия меню больше не приводит к
  падению процесса.
* В `ModMenuAdapter.onBindViewHolder()` добавлена защита от отсутствующих
  view в item-разметке.

## 2. TriggerBot

* Решение об ударе считается нативно по пересечению луча прицела
  (`ActorRotationComponent`) с мировым AABB актёра (slab method), а не по
  углу до цели aim assist. Работает при выключенном AimAssist.
* Список актёров теперь гарантированно содержит и мобов, и игроков:
  `Level::fetchNearbyActors` вызывается сначала с mob-маской, а если
  игроков в списке нет — дополнительно с полной маской; дубликаты
  отсекаются.
* Если прицел уже внутри хитбокса, aim assist обязан вести именно этого
  актёра (`triggerTarget` priority в `findTargetInternal`). Раньше
  доводка могла утащить камеру с хитбокса и удар отменялся.
* Допуск по хитбоксу уменьшен до 0.03 блока (компенсация интерполяции
  позиции между тиками), дальность — 3.0 блока до поверхности.
* `TriggerBotOverlay` не стреляет сквозь открытое меню клиента
  (`InbuiltOverlayManager.isModMenuShowing()`), паузу и HUD.
* Удар отправляется штатным путём игры: `performInbuiltAttack()` шлёт
  настоящий `MotionEvent` DOWN → MOVE → UP в `super.dispatchTouchEvent`.
  Урон, пакет атаки и анимация руки формируются самим клиентом.
  Координаты берутся из `decorView`, добавлен промежуточный MOVE, чтобы
  жест гарантированно склеивался gesture-детектором игры.

## 3. Обход серверных анти-читов (Polar / ML / Geyser)

Ключевой принцип: **никаких синтезированных пакетов**. Всё, что видит
сервер, генерирует сам ванильный клиент:

* поворот камеры идёт через штатный `applyTurnDelta` (тот же путь, что и
  ручной ввод), поэтому `PlayerAuthInput`/`MovePlayer` формируются игрой;
* `sendMovePacket()` остаётся no-op, если sender не зарегистрирован —
  лишних пакетов с «идеальными» углами нет.

Добавлен humanization-слой (`AimAssist`):

* реакция на новую цель 120–200 мс (база + случайный разброс) — камера не
  «прилипает» в кадр появления цели;
* микро-тремор камеры ~0.02° (две несоизмеримые синусоиды) — траектория
  перестаёт быть идеально прямой, это ломает эвристики rotation/aim
  pattern;
* ограничение поворота `maxTickDeltaDeg = 40°` за серверный тик (50 мс) —
  защита от rotation-снапов;
* сохранены ограничение скорости/ускорения, deadzone, sticky-target,
  рандомный CPS (620–790 мс), 6% пропусков и случайная задержка удара из
  `TriggerBotOverlay`.

Настройки доступны из JNI: `nativeSetHumanization`,
`nativeSetReactionDelay`, `nativeSetAimTremor` (обёртки в `AimAssistMod`).

Для Geyser это означает, что Bedrock-клиент не отправляет в прокси
аномальных углов/пакетов: Java-сервер видит обычный клиентский ввод.

## Сборка

`app/src/main/cpp/preloader` и `libHttpClient` в архиве — пустые каталоги
(submodules). Перед сборкой:

```
git submodule update --init --recursive
```

Далее Android SDK + NDK, `./gradlew assembleDebug` либо GitHub Actions
(тег `esp-debug`).

Проверка C++ (выполнена на clang 21, `-std=c++20 -fsyntax-only`):
`AimAssist.cpp`, `ClientBridge.cpp`, `Esp.cpp`, `aimassist.cpp` — без
ошибок.

## Что проверить на устройстве

* Версия игры 1.26.60.x (оффсеты/сигнатуры сняты с 1.26.60.4).
* `adb logcat -s AimAssist AimAssistBridge TriggerBot ModMenuOverlay`.
* Меню должно открываться без падения; если что-то пойдёт не так — в логе
  будет `ModMenuOverlay: showInternal failed ...` с причиной.
* `isOnTarget` становится true, когда прицел внутри хитбокса игрока/моба в
  пределах 3 блоков; удар сопровождается анимацией руки.
## 4. TriggerBot: почему он «вообще не бил» (найдено позже)

Две реальные причины, обе в Java-слое:

1. **Модуль не был зарегистрирован в оверлей-менеджере.**
   `InbuiltOverlayManager.showModOverlay()` не имел `case ModIds.TRIGGER_BOT`,
   `KEYBIND_MOD_IDS` не содержал `TRIGGER_BOT`, а
   `InbuiltModManager.getDefaultInbuiltModKeybind()` не задавал дефолтную
   клавишу. Итог: включение модуля в меню ничего не показывало, а shortcut
   отсутствовал. Добавлено: создание/показ/скрытие `TriggerBotOverlay`,
   регистрация в keybind-списке, дефолтная клавиша **G**, восстановление
   позиции и состояния.

2. **Инвертированная проверка HUD в цикле ударов.**
   `PreloaderInput.isHudScreenOpen()` == «идёт обычный геймплей» (так его
   используют `showGameOverlays`, `handleKeyEvent` и `pojavIsMenuOpen`).
   Старый guard останавливал триггер именно в геймплее:
   `if (isHudScreenOpen() ...) return;`. Исправлено на
   `if (!isHudScreenOpen() || isPauseMenuOpen() || isShowingMenu() || isModMenuShowing())`.

Дополнительно: расширены настройки (10 параметров, категории
`trigger_target` / `trigger_timing` / `appearance`):
задержка и разброс реакции, дальность, допуск хитбокса, бить игроков,
бить мобов, humanized-реакция, интервал и его разброс, шанс промаха,
клавиша активации. Все значения сохраняются в prefs и применяются нативно
через `AimAssistMod` (общие настройки триггера, Aim Assist не трогаем).

## 6. TriggerBot: найдена корневая причина «вообще не бьёт»

Разбор `libminecraftpe.so` 1.26.60.04_RC3 (ARM64) показал два неверных
допущения в нативной части:

1. **`Level::fetchNearbyActors` принимает `Level*`, а не `Actor*`.**
   В `ClientBridge::appendNearbyActors` первым аргументом передавался
   локальный игрок. По дизассемблеру функции:
   - `x0` — `Level*` (код перед вызовом делает `ldr x0, [actor, #0x28]`);
   - `x1` — `Vec3 const*` (полуразмеры, читаются как `ldp s1,s2,[x1]`);
   - `x2` — маска типа (`0x100` мобы, `0x40`, `0x13f`, `-1`);
   - `x8` — `std::vector<DistanceSortedActor>` (sret).
   Из Actor объект уровня берётся по оффсету `+0x28`, поэтому добавлены
   `offsets::kActorLevel = 0x28`, `offsets::kLevelBounds = 0x230`
   (`Level+0x230` — указатель на 6 float границ) и
   `ClientBridge::getLevel()`.
   `DistanceSortedActor` подтверждён как 16 байт (`asr x8, x25, #4` в
   самом `fetchNearbyActors`), `ActorVec` — `{begin, end, cap}`.

2. **`ClientInstance::getLocalPlayer` резолвился не в ту функцию.**
   Найденная по сигнатуре функция копирует `std::vector` из
   `ClientInstance+0x240` и возвращает `bool`; её результат
   интерпретировался как `Actor*`, то есть локальным игроком становился
   указатель на стек. После этого `getViewAngles()`/`getEyePosition()`
   всегда падали, `updateTrigger()` не вызывался, `isOnTarget()` был
   вечно `false` — отсюда «не бьёт».
   Исправления:
   - результат сигнатуры проверяется по vtable (`dladdr` →
     `libminecraftpe.so`), а не принимается на веру;
   - добавлен резервный поиск `LocalPlayer` сканированием полей
     `ClientInstance`: кандидат должен быть Actor (`isPlayer`), иметь
     валидный `+0x28` (Level), у которого `+0x230` указывает на
     правдоподобные границы, и читаемый `ActorRotationComponent`;
   - `clientUpdateDetour` больше не перезаписывает кеш игрока
     результатом ошибочной сигнатуры; при смене `ClientInstance` кеш
     сбрасывается.

3. Триггер стал щедрее к касанию: допуск `triggerBoxTolerance`
   применяется по всем осям (X, Y, Z), а не половиной по вертикали —
   луч, задевший хитбокс краем, уже считается попаданием.

Диагностика (logcat):
- `adb logcat -s AimAssist AimAssistBridge TriggerBot`
- `AimAssistBridge`: `fetchNearbyActors(...)-> begin/end/cap`,
  `local player resolved by scan: client+0xNN -> ...`
- `AimAssist`: `trigger: player=… actors=… considered=… rejectedType=…
  rejectedBounds=… eye=… dir=… view=… hit=… onTarget=…` (раз в секунду)
- `TriggerBot`: `loop: hudOpen=… pause=… showingMenu=… modMenu=…
  onTarget=…`, `attack dispatched=…`
