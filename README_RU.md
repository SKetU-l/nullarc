# Драйвер AMD RDNA 3 (Radeon 780M / GFX1103) для macOS

[![macOS](https://img.shields.io/badge/macOS-Tahoe%2026.6.2%20%2F%20Sonoma-success?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Архитектура](https://img.shields.io/badge/Архитектура-RDNA%203%20%2F%20GFX1103-red?logo=amd&logoColor=white)](https://www.amd.com/)
[![Metal](https://img.shields.io/badge/Metal-3D%20%26%20Compute%20Активен-orange?logo=apple&logoColor=white)](https://developer.apple.com/metal/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.363%20(RADV)-red?logo=vulkan&logoColor=white)](https://mesa3d.org/)
[![Статус](https://img.shields.io/badge/Веха-M2%20Активна%20(WindowServer%20Metal)-brightgreen)](https://github.com/luuumi-dev/amd-rdna3-macos-driver)
[![Релизы](https://img.shields.io/github/v/release/luuumi-dev/amd-rdna3-macos-driver?color=blue&logo=github)](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases)

Язык: **[English (Английский)](README.md)** | **Русский**

Первый в мире графический драйвер с открытым исходным кодом и низкоуровневая система инициализации оборудования, обеспечивающие **3D-ускорение Metal, вычисления Metal Compute и аппаратный композитинг рабочего стола WindowServer** для **встроенной графики AMD RDNA 3 (Radeon 780M / APU Phoenix и Hawk Point, GFX1103 / GC 11.0.1)** на **macOS Tahoe (Darwin 25.x)** и macOS Sonoma.

---

## 🧪 Руководство по установке и тестированию (Quickstart)

> [!WARNING]
> Данный драйвер является альфа-версией и предназначен для тестеров, разработчиков и энтузиастов Hackintosh, уверенно работающих с OpenCore и инструментами восстановления macOS.

### 1. Требования к оборудованию
* **Процессор (APU):** AMD Ryzen 7 7840HS, 7840U, 8845HS, 8840U или совместимые чипы семейств Phoenix / Hawk Point.
* **Видеоядро (iGPU):** AMD Radeon 780M (`1002:15bf`, архитектура GC 11.0.1 / GFX1103).
* **Экран:** Встроенная матрица ноутбука eDP (2560x1600 или 1920x1080).
* **Операционная система:** macOS Tahoe (26.x / Darwin 25.x) или macOS Sonoma (14.x).

### 2. Загрузка компонентов релиза
Скачайте актуальные архивы со [страницы релизов GitHub](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases):
* **`AMD-RDNA3-Radeon-780M-v0.2.0-alpha.zip`** *(Рекомендуется)*: Полный комплект, содержащий все три необходимых компонента (`PhxBringup.kext`, `PhxAccel.kext` и `NVMTLDriver.bundle`).
* **`PhxBringup-v0.2.0-alpha.zip`**: Отдельный кекст только для загрузчика OpenCore EFI (для тестирования базовой инициализации ядра и Metal Compute).

### 3. Настройка загрузчика OpenCore
В файле `config.plist` вашего OpenCore в секции `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args` добавьте следующие параметры:

```text
keepsyms=1 revpatch=cpuname,sbvmm ipc_control_port_options=0 -amfipassbeta amfi_get_out_of_my_way=0x1 amfi=0x80 phxdebug=1
```

* `amfi=0x80 amfi_get_out_of_my_way=0x1`: **Обязательно.** Отключает проверку целостности библиотек Apple Mobile File Integrity, позволяя системному процессу `WindowServer` и программам загружать сторонний Metal-бандл.
* `phxdebug=1`: Активирует диагностические эндпоинты `sysctl` и низкоуровневые инструменты инициализации.

### 4. Установка компонентов драйвера

Для полной работы аппаратного ускорения требуются три компонента:

1. **Кекст ядра для EFI (`PhxBringup.kext`):**
   Скопируйте `PhxBringup.kext` на ваш EFI-раздел в папку `EFI/OC/Kexts/` и пропишите его в `config.plist` в секции `Kernel -> Add`.
2. **Системный кекст акселератора и фреймбуфера (`PhxAccel.kext`):**
   Скопируйте `PhxAccel.kext` в папку `/Library/Extensions/` и обновите Auxiliary Kernel Collection:
   ```bash
   sudo cp -R PhxAccel.kext /Library/Extensions/
   sudo chown -R root:wheel /Library/Extensions/PhxAccel.kext
   sudo kmutil install --volume-root / --repository /Library/Extensions
   ```
3. **Бандл трансляции Metal (`NVMTLDriver.bundle`):**
   Скопируйте `NVMTLDriver.bundle` в системную папку GPU-бандлов:
   ```bash
   sudo mkdir -p /Library/GPUBundles
   sudo cp -R NVMTLDriver.bundle /Library/GPUBundles/
   sudo chown -R root:wheel /Library/GPUBundles/NVMTLDriver.bundle
   ```

### 5. Проверка работы Metal и композитора
После перезагрузки откройте Терминал и выполните диагностические команды:

```bash
# 1. Тест вычислений Metal Compute (4096 параллельных потоков на 780M)
./tools/mtlprobe/mtlprobe compute

# Ожидаемый вывод:
# [MTLPROBE] Device: AMD Radeon 780M Graphics (RADV PHOENIX)
# [MTLPROBE] 4096/4096 correct.

# 2. Тест 3D-растеризации геометрии Metal
./tools/mtlprobe/mtlprobe render

# Ожидаемый вывод:
# [MTLPROBE] 3D pipeline compiled and executed on 780M.
# [MTLPROBE] pixels: red 32640 blue 32896 other 0

# 3. Проверка счётчика аппаратных кадров WindowServer
sysctl debug.nvaccel_iop_ok debug.nvaccel_iop_fail debug.nvaccel_iop_copy_us

# Ожидаемый вывод:
# debug.nvaccel_iop_ok: > 1000  (кадры, аппаратно отрендеренные 780M)
# debug.nvaccel_iop_fail: 0
# debug.nvaccel_iop_copy_us: ~1500 (1.5 мс на вывод кадра)
```

> [!TIP]
> **Примечание по экрану входа:** В текущих альфа-сборках окно входа (`loginwindow`) может отображаться тёмным до момента авторизации. Рекомендуется включить **Автоматический вход** в настройках «Пользователи и группы» macOS, чтобы система сразу открывала ускоренный рабочий стол.

---

## 🌟 Достигнутые вехи проекта (Октябрь 2026)

Apple прекратила разработку драйверов AMD после RDNA 2 (Navi 21/23). Поколение RDNA 3 (Phoenix, Hawk Point, Strix Point) никогда официально не поддерживалось. Этот проект закрывает этот пробел:

| Веха | Статус | Подробности |
| :--- | :---: | :--- |
| **Кремний и микрокод (E1–E5)** | ✅ **ГОТОВО** | Автозагрузка прошивок PSP 13.0.4, RLC, SMU GfxImu, планировщик MES v11 (Micro Engine Scheduler), DMA-передачи через SDMA. |
| **Виртуальная память GPU (E7)** | ✅ **ГОТОВО** | 4-уровневые таблицы страниц GPUVM в оперативной памяти с аппаратной когерентностью кэшей, изоляция процессов (VMID 8..15). |
| **Vulkan 1.4 (E8 / RADV)** | ✅ **ГОТОВО** | Нативная Mesa RADV под Darwin с шейдерным компилятором ACO; подтверждён запуск шейдеров через `vkprobe`. |
| **Metal 3D и Compute (M1)** | ✅ **ГОТОВО** | Аппаратные вычисления Metal compute (`mtlprobe` 4096/4096) и 3D-растеризация на кремнии (`AMD Radeon 780M Graphics`). |
| **Композитинг WindowServer (M2)** | ✅ **АКТИВНО** | `WindowServer` рендерит весь интерфейс macOS через `CompositorMetal` на 780M. Нагрузка на CPU около 0%, скорость до 41+ FPS. |
| **Дисплейный пайплайн и AGDC** | ✅ **ГОТОВО** | Собственный фреймбуфер `PhxAMDFB`, контроллер `PhxAGDC`, потоковый сброс кадров без задержек (~1.5 мс/кадр). |
| **Официальное имя и профилировщик** | ✅ **ГОТОВО** | Кекст Build 28 публикует `AMD Radeon 780M` и объём VRAM напрямую в IORegistry и окно «Об этом Mac». |

---

## 🏛️ Архитектура драйвера

Стек состоит из трёх скоординированных уровней, связывающих закрытые графические фреймворки macOS с кремнием AMD RDNA 3:

```
┌────────────────────────────────────────────────────────────────────────┐
│               Пользовательские приложения macOS                        │
│     (Finder, Safari, Metal-приложения, композитор WindowServer)        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Metal API
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  /Library/GPUBundles/NVMTLDriver.bundle (Трансляция Metal)             │
│  ├── libnvmtl_translate.dylib                                          │
│  └── /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib (Mesa RADV 1.4)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ IOKit UserClient ABI
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  Кексты ядра macOS (IOKit / Mach-O):                                   │
│  ├── PhxBringup.kext (Build 28 в EFI):                                 │
│  │   ├── Загрузка прошивок через кольцо PSP 13.0.4 и память TMR        │
│  │   ├── Управление планировщиком MES v11 (KIQ) и очереди команд       │
│  │   └── 4-уровневый аллокатор таблиц страниц GPUVM (VMID 8..15)       │
│  └── PhxAccel.kext (в Auxiliary Kernel Collection):                    │
│      ├── PhxAMDFB (фреймбуфер экрана с тегом вендора AMD)              │
│      ├── PhxAGDC (взаимодействие с AppleGraphicsDeviceControl)         │
│      └── NMXAccel / NVDisplayPipe (ускоренный вывод кадров)            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ MMIO BAR0 / Doorbell BAR2
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│          Кремний AMD Radeon 780M (Hawk Point / Phoenix GFX1103)        │
│          Матрица дисплея DCN 3.1.4 (2560x1600 @ 120Hz eDP)             │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 👥 Авторы и благодарности

* **Ведущий мейнтейнер и системное тестирование:** [@luuumi-dev](https://github.com/luuumi-dev)
* **Низкоуровневая разработка ядра и драйвера:** Anthropic Claude (Opus 5.5) и Google DeepMind Gemini (Antigravity)
* **Архитектурные исследования графики macOS:** [Cobalt64](https://cobalt64.com) (RDNA4 / Tahoe Metal) и **NullMoth** (архитектура IOAccelerator)
* **Аппаратная спецификация и открытый код:** Команда разработчиков `amdgpu` в ядре Linux и проект Mesa 3D (команда RADV ACO)
* **Сообщество OpenCore и Hackintosh:** [Acidanthera](https://github.com/acidanthera) и сообщество AMD-OSX

---

## 📄 Лицензия

Исходный код репозитория распространяется под лицензией MIT License. Части, унаследованные от NullMoth, регулируются лицензией PolyForm Noncommercial License 1.0.0. Компоненты Mesa RADV распространяются под лицензией MIT/X11.
