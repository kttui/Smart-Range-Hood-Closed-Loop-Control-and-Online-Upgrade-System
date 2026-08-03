@echo off
setlocal

if "%KEIL_ROOT%"=="" set "KEIL_ROOT=D:\Keil_v5"
set "ARM_BIN=%KEIL_ROOT%\ARM\ARMCC\bin"
set "ROOT=%~dp0.."
set "OUT=%~dp0Objects"

if not exist "%ARM_BIN%\armcc.exe" (
  echo ARMCC not found under %ARM_BIN%
  exit /b 2
)
if not exist "%OUT%" mkdir "%OUT%"

set "INCLUDES=-I%ROOT%\Start -I%ROOT%\User -I%ROOT%\Library -I%ROOT%\Common -I%ROOT%\BSP -I%ROOT%\Bootloader"
set "CFLAGS=--c99 --cpu Cortex-M3 -O2 --apcs=interwork --split_sections --littleend -DSTM32F10X_HD -DUSE_STDPERIPH_DRIVER -DVECT_TAB_OFFSET=0 %INCLUDES%"

for %%F in (
  "%ROOT%\Bootloader\boot_main.c"
  "%ROOT%\Bootloader\boot_sha256.c"
  "%ROOT%\Bootloader\boot_p256.c"
  "%ROOT%\Bootloader\boot_crypto.c"
  "%ROOT%\Bootloader\boot_flash.c"
  "%ROOT%\Bootloader\boot_metadata.c"
  "%ROOT%\Common\ota_protocol.c"
  "%ROOT%\Common\ota_boot_control.c"
  "%ROOT%\Start\system_stm32f10x.c"
  "%ROOT%\Start\core_cm3.c"
  "%ROOT%\Library\stm32f10x_rcc.c"
  "%ROOT%\Library\stm32f10x_gpio.c"
  "%ROOT%\Library\stm32f10x_usart.c"
  "%ROOT%\Library\stm32f10x_flash.c"
  "%ROOT%\Library\stm32f10x_pwr.c"
  "%ROOT%\Library\stm32f10x_bkp.c"
) do (
  echo Compiling %%~nxF
  "%ARM_BIN%\armcc.exe" %CFLAGS% -c "%%~F" -o "%OUT%\%%~nF.o"
  if errorlevel 1 exit /b 1
)

"%ARM_BIN%\armasm.exe" --cpu Cortex-M3 --littleend "%ROOT%\Start\startup_stm32f10x_hd.s" -o "%OUT%\startup_stm32f10x_hd.o"
if errorlevel 1 exit /b 1

set "OBJECTS=%OUT%\startup_stm32f10x_hd.o %OUT%\boot_main.o %OUT%\boot_sha256.o %OUT%\boot_p256.o %OUT%\boot_crypto.o %OUT%\boot_flash.o %OUT%\boot_metadata.o %OUT%\ota_protocol.o %OUT%\ota_boot_control.o %OUT%\system_stm32f10x.o %OUT%\core_cm3.o %OUT%\stm32f10x_rcc.o %OUT%\stm32f10x_gpio.o %OUT%\stm32f10x_usart.o %OUT%\stm32f10x_flash.o %OUT%\stm32f10x_pwr.o %OUT%\stm32f10x_bkp.o"
"%ARM_BIN%\armlink.exe" --cpu Cortex-M3 --strict --scatter "%~dp0Bootloader.sct" --map --list "%OUT%\Bootloader.map" --output "%OUT%\Bootloader.axf" %OBJECTS%
if errorlevel 1 exit /b 1

"%ARM_BIN%\fromelf.exe" --i32combined --output "%OUT%\Bootloader.hex" "%OUT%\Bootloader.axf"
if errorlevel 1 exit /b 1
"%ARM_BIN%\fromelf.exe" --bin --output "%OUT%\Bootloader.bin" "%OUT%\Bootloader.axf"
if errorlevel 1 exit /b 1
"%ARM_BIN%\fromelf.exe" --text -z "%OUT%\Bootloader.axf"

echo Bootloader build completed.
endlocal
