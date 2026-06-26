<?php

declare(strict_types=1);

/*
 * Build command for Termux / Linux:
 *   php -d phar.readonly=0 build.php
 *
 * The generated file will be:
 *   BlockPrivates.phar
 */

$root = __DIR__;
$output = $root . DIRECTORY_SEPARATOR . 'BlockPrivates.phar';
$requiredFiles = [
    'plugin.yml',
    'src/PrivateBlocks/BlockPrivates.php',
    'src/PrivateBlocks/HologramRefreshTask.php',
    'src/PrivateBlocks/HologramSendTask.php',
];

if(ini_get('phar.readonly') === '1'){
    fwrite(STDERR, "phar.readonly включён. Запусти так: php -d phar.readonly=0 build.php\n");
    exit(1);
}

foreach($requiredFiles as $file){
    if(!is_file($root . DIRECTORY_SEPARATOR . $file)){
        fwrite(STDERR, "Не найден обязательный файл: {$file}\n");
        exit(1);
    }
}

if(file_exists($output) && !unlink($output)){
    fwrite(STDERR, "Не удалось удалить старый файл: {$output}\n");
    exit(1);
}

$phar = new Phar($output);
$phar->startBuffering();
$phar->addFile($root . DIRECTORY_SEPARATOR . 'plugin.yml', 'plugin.yml');

$sourceDirectory = $root . DIRECTORY_SEPARATOR . 'src';
$files = new RecursiveIteratorIterator(
    new RecursiveDirectoryIterator($sourceDirectory, FilesystemIterator::SKIP_DOTS),
    RecursiveIteratorIterator::LEAVES_ONLY
);

foreach($files as $file){
    if(!$file instanceof SplFileInfo || !$file->isFile()){
        continue;
    }

    $path = $file->getPathname();
    $localPath = str_replace($root . DIRECTORY_SEPARATOR, '', $path);
    $phar->addFile($path, str_replace(DIRECTORY_SEPARATOR, '/', $localPath));
}

$phar->setStub("<?php __HALT_COMPILER();\n");
$phar->compressFiles(Phar::GZ);
$phar->stopBuffering();

clearstatcache(true, $output);
echo "Готово: " . basename($output) . "\n";
echo "Размер: " . filesize($output) . " байт\n";
echo "Закинь " . basename($output) . " в папку plugins на сервере PocketMine-MP.\n";
