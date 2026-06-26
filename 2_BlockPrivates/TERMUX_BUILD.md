# Инструкция: как собрать BlockPrivates.phar в Termux с нуля

Эта инструкция нужна, чтобы собрать плагин PocketMine-MP `BlockPrivates` в файл `BlockPrivates.phar` прямо на телефоне через Termux.

## 1. Установи Termux

Лучше устанавливать Termux не из Google Play, а из F-Droid или GitHub, потому что версия в Google Play часто устаревшая.

После установки открой Termux.

## 2. Обнови пакеты Termux

Введи команды по очереди:

```bash
pkg update -y
pkg upgrade -y
```

## 3. Установи PHP и нужные утилиты

Введи:

```bash
pkg install php git nano -y
```

Проверь, что PHP установлен:

```bash
php -v
```

## 4. Создай папку для плагина

```bash
mkdir -p ~/BlockPrivates
cd ~/BlockPrivates
```

## 5. Создай файл plugin.yml

```bash
nano plugin.yml
```

Вставь туда содержимое файла `plugin.yml` из проекта, затем сохрани файл:

- нажми `CTRL + X`;
- нажми `Y`;
- нажми `ENTER`.

## 6. Создай папку с кодом плагина

```bash
mkdir -p src/PrivateBlocks
```

## 7. Создай PHP-файлы плагина

Создай главный файл:

```bash
nano src/PrivateBlocks/BlockPrivates.php
```

Вставь код файла `src/PrivateBlocks/BlockPrivates.php` из проекта и сохрани через `CTRL + X`, `Y`, `ENTER`.

Создай файл задачи обновления голограмм:

```bash
nano src/PrivateBlocks/HologramRefreshTask.php
```

Вставь код файла `src/PrivateBlocks/HologramRefreshTask.php` из проекта и сохрани.

Создай файл задачи отправки голограмм:

```bash
nano src/PrivateBlocks/HologramSendTask.php
```

Вставь код файла `src/PrivateBlocks/HologramSendTask.php` из проекта и сохрани.

## 8. Создай файл build.php

```bash
nano build.php
```

Вставь код файла `build.php` из проекта и сохрани.

## 9. Проверь структуру папок

В папке `~/BlockPrivates` должно быть так:

```text
BlockPrivates/
├── build.php
├── plugin.yml
└── src/
    └── PrivateBlocks/
        ├── BlockPrivates.php
        ├── HologramRefreshTask.php
        └── HologramSendTask.php
```

Проверить можно командой:

```bash
find . -maxdepth 3 -type f
```

## 10. Собери PHAR

Находясь в папке `~/BlockPrivates`, запусти:

```bash
php -d phar.readonly=0 build.php
```

Если всё правильно, появится файл:

```text
BlockPrivates.phar
```

## 11. Проверь, что PHAR создался

```bash
ls -lh BlockPrivates.phar
```

Можно дополнительно проверить содержимое PHAR:

```bash
php -r '$p=new Phar("BlockPrivates.phar"); echo $p->offsetExists("plugin.yml") && $p->offsetExists("src/PrivateBlocks/BlockPrivates.php") ? "phar ok\n" : "phar missing files\n";'
```

Если пишет `phar ok`, файл собран правильно.

## 12. Установи плагин на сервер PocketMine-MP

Скопируй `BlockPrivates.phar` в папку `plugins` твоего сервера PocketMine-MP.

Пример, если сервер лежит в папке `~/server`:

```bash
cp BlockPrivates.phar ~/server/plugins/
```

Потом перезапусти сервер.

## Частые ошибки

### Ошибка: `phar.readonly`

Если PHAR не собирается из-за `phar.readonly`, запускай именно так:

```bash
php -d phar.readonly=0 build.php
```

### Ошибка: нет файла plugin.yml

Значит ты запускаешь сборку не из папки плагина или не создал `plugin.yml`. Перейди в папку:

```bash
cd ~/BlockPrivates
```

И снова запусти сборку.

### Ошибка: класс не найден

Проверь, что путь к главному файлу именно такой:

```text
src/PrivateBlocks/BlockPrivates.php
```

И что в `plugin.yml` указан правильный главный класс:

```yaml
main: PrivateBlocks\BlockPrivates
```
