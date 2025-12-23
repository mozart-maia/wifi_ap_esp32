@echo off
echo ========================================
echo   BUILD, FLASH E MONITOR ESP32
echo ========================================
echo.

echo [1/3] Compilando...
idf.py build
if %errorlevel% neq 0 (
    echo ERRO ao compilar!
    pause
    exit /b 1
)

echo.
echo [2/3] Preparando para gravar...
echo.
echo 1. SEGURE o botao BOOT
echo 2. PRESSIONE e SOLTE o botao RESET
echo 3. SOLTE o botao BOOT
echo 4. PRESSIONE QUALQUER TECLA AQUI
echo.
pause

echo.
echo Gravando firmware...
esptool.py -p COM4 -b 115200 --before no_reset --after hard_reset write_flash 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/wifi-ap.bin

if %errorlevel% neq 0 (
    echo.
    echo ERRO ao gravar! Tente novamente.
    pause
    exit /b 1
)

echo.
echo [3/3] Iniciando monitor serial...
echo (Pressione Ctrl+] para sair)
echo.
timeout /t 2
idf.py -p COM4 monitor