# Проект драйвера и инициализации AMD RDNA 3 (Radeon 780M / GFX1103) для macOS

[![macOS](https://img.shields.io/badge/macOS-Sonoma%2014.x%20%2F%20Tahoe%2025.x-success?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Architecture](https://img.shields.io/badge/Architecture-RDNA%203%20%2F%20GFX1103-red?logo=amd&logoColor=white)](https://www.amd.com/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.363%20(RADV)-red?logo=vulkan&logoColor=white)](https://mesa3d.org/)
[![CI](https://github.com/luuumi-dev/amd-rdna3-macos-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/luuumi-dev/amd-rdna3-macos-driver/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/luuumi-dev/amd-rdna3-macos-driver?color=blue&logo=github)](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases)
[![License](https://img.shields.io/badge/License-Multi--License-lightgrey.svg)](LICENSE)

Язык: **[English (Original)](README.md)** | **Русский**

Экспериментальный открытый стек драйверов и низкоуровневой аппаратной инициализации для **интегрированной графики AMD RDNA 3 (Radeon 780M / APU Phoenix и Hawk Point, GFX1103 / GC 11.0.1)** в среде **macOS Sonoma (14.x)** и **macOS Tahoe (Darwin 25.x)**.

> [!NOTE]
> **Фактический статус на реальном железе:**
> Тесты вычислений Metal успешно проходят (`mtlprobe compute`, 4096/4096 рабочих групп), композитинг рабочего стола `WindowServer` работает на графике Radeon 780M на тестовом ноутбуке **Firebat U6C** (AMD Ryzen 7 7840HS / 8845HS). Другие ноутбуки, портативные консоли, модели APU и настольные конфигурации в настоящий момент **не тестировались**.

---

## 🧪 Инструкция по установке для ранних тестировщиков (Quickstart)

> [!WARNING]
> Данный драйвер находится на стадии альфа-тестирования. Отключение встроенных механизмов безопасности macOS несет реальные риски. Обязательно ознакомьтесь с разделом [Риски безопасности и отключение AMFI](#-риски-безопасности-и-отключение-amfi) перед установкой.

### 1. Поддерживаемое железо
* **Целевые процессоры:** AMD Ryzen 7 7840HS, 7840U, 8845HS, 8840U (линейки Phoenix и Hawk Point).
* **Графическое ядро:** AMD Radeon 780M (`1002:15bf` / `1002:1900`, GC 11.0.1 / GFX1103).
* **Эталонное устройство:** Firebat U6C (встроенная матрица 16" 2560x1600 eDP). На других платформах работоспособность экспериментальна.

### 2. Загрузка файлов релиза
Скачайте готовые сборки со страницы [GitHub Releases](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases):
* **[`AMD-RDNA3-Radeon-780M-macOS-Full-Package-v0.2.1-alpha.zip`](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases/download/v0.2.1-alpha/AMD-RDNA3-Radeon-780M-macOS-Full-Package-v0.2.1-alpha.zip)** *(Рекомендуется)*: Полный пакет, содержащий `PhxBringup.kext` для OpenCore, `PhxAccel.kext` для `/Library/Extensions` и транслятор `NVMTLDriver.bundle` для `/Library/GPUBundles`.
* **[`PhxBringup.kext-v0.2.1-alpha.zip`](https://github.com/luuumi-dev/amd-rdna3-macos-driver/releases/download/v0.2.1-alpha/PhxBringup.kext-v0.2.1-alpha.zip)**: Автономный кекст для OpenCore для отладки инициализации кремния и командных колец.

### 3. Настройка OpenCore `boot-args`
Добавьте аргументы загрузки в ваш `config.plist` по пути `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args`:

```text
keepsyms=1 revpatch=cpuname,sbvmm ipc_control_port_options=0 -amfipassbeta amfi_get_out_of_my_way=0x1 amfi=0x80 phxdebug=1
```

Очищенный от персональных данных эталонный конфиг доступен в [`hardware/Sample.plist`](hardware/Sample.plist). Список сторонних необходимых кекстов с официальными ссылками находится в [`hardware/REQUIRED_KEXTS.md`](hardware/REQUIRED_KEXTS.md).

### 4. Развертывание компонентов драйвера

1. **Кекст для OpenCore (`PhxBringup.kext`):**
   Скопируйте `PhxBringup.kext` в папку `EFI/OC/Kexts/` на вашем EFI-разделе и пропишите его в `config.plist` в секции `Kernel -> Add`.
2. **Системный кекст акселератора (`PhxAccel.kext`):**
   Скопируйте `PhxAccel.kext` в `/Library/Extensions/` и обновите системную коллекцию кекстов:
   ```bash
   sudo cp -R PhxAccel.kext /Library/Extensions/
   sudo chown -R root:wheel /Library/Extensions/PhxAccel.kext
   sudo kmutil install --volume-root / --repository /Library/Extensions
   ```
3. **Бандл трансляции Metal (`NVMTLDriver.bundle`):**
   Скопируйте `NVMTLDriver.bundle` и каталог `nvmtl` в `/Library/GPUBundles/`:
   ```bash
   sudo mkdir -p /Library/GPUBundles
   sudo cp -R NVMTLDriver.bundle /Library/GPUBundles/
   sudo chown -R root:wheel /Library/GPUBundles/NVMTLDriver.bundle
   ```

### 5. Команды для проверки в Terminal

После перезагрузки откройте Terminal для проверки работы драйвера:

```bash
# 1. Сборка и запуск Metal-теста (4096 рабочих групп на 780M).
#    Имя бинарника должно остаться "mtlprobe": оно есть в списке разрешённых процессов NVMTLDriver.
clang -fobjc-arc -framework Foundation -framework Metal \
      -o tools/mtlprobe/mtlprobe tools/mtlprobe/mtlprobe.m
./tools/mtlprobe/mtlprobe compute

# Ожидаемый вывод:
# [MTLPROBE] Device: AMD Radeon 780M Graphics (RADV PHOENIX)
# [MTLPROBE] 4096/4096 correct.

# 2. Проверка аппаратных счетчиков кадров WindowServer
sysctl debug.nvaccel_iop_ok debug.nvaccel_iop_fail debug.nvaccel_iop_copy_us

# Ожидаемый результат:
# debug.nvaccel_iop_ok: > 1000  (кадры, отрендеренные ускорителем)
# debug.nvaccel_iop_fail: 0
# debug.nvaccel_iop_copy_us: ~1500 (1.5 мс потоковое копирование кадра)
```

---

## ⚠️ Риски безопасности и отключение AMFI

Для загрузки сторонних графических трансляторов в системный процесс `WindowServer` в macOS Sonoma и Tahoe необходимо передавать флаги `amfi=0x80` и `amfi_get_out_of_my_way=0x1`.

### Что при этом отключается
* **Валидация библиотек (Library Validation):** Ослабление подсистемы Apple Mobile File Integrity (AMFI) позволяет системным процессам загружать библиотеки без подписи Apple или со сторонними ad-hoc сертификатами.
* **Неработающие сервисы:** Системные функции, требующие жесткой проверки целостности платформы, перестают работать:
  * Воспроизведение DRM-контента FairPlay (Apple TV+, Netflix 4K в Safari).
  * Некоторые приложения из Mac App Store и инструменты разработчиков с включенным Hardened Runtime.
* **Обновления системы:** Установка инкрементных OTA-обновлений macOS может завершаться с ошибкой при отключенном AMFI и модифицированном SIP.

### Как вернуться к стандартному состоянию
Если вам потребуется вернуть штатные механизмы безопасности macOS:
1. Пока система загружена с текущими `boot-args`, удалите компоненты драйвера:
   ```bash
   sudo rm -rf /Library/Extensions/PhxAccel.kext /Library/GPUBundles/NVMTLDriver.bundle /Library/GPUBundles/nvmtl
   sudo kmutil install --volume-root / --update-all
   ```
   Если система попросит, подтвердите изменение в «Системные настройки → Конфиденциальность и безопасность», затем перезагрузитесь.
2. Удалите `PhxBringup.kext` из `EFI/OC/Kexts/` и его запись в `Kernel -> Add` в `config.plist`.
3. Уберите `amfi=0x80`, `amfi_get_out_of_my_way=0x1` и `-amfipassbeta` из `boot-args` и перезагрузитесь.

Порядок важен: если сначала включить AMFI, `WindowServer` при следующем запуске откажется загружать bundle с ad-hoc подписью.

---

## 🔍 Что такое `NVMTLDriver.bundle`?

Несмотря на префикс `NV` в названии файла, **`NVMTLDriver.bundle` НЕ является драйвером NVIDIA**.

Префикс остался в наследство от исследовательской кодовой базы **NullMoth**, разработавшего архитектуру трансляции Vulkan в Metal для macOS. В данном проекте `NVMTLDriver.bundle` выполняет роль моста-транслятора:
1. Он перехватывает вызовы Metal API от `WindowServer` и графических приложений macOS.
2. Транслирует их в команды Vulkan, исполняемые адаптированным под Darwin драйвером **Mesa RADV** (`libvulkan_radeon.dylib`).
3. Mesa RADV отправляет команды напрямую в аппаратные кольца AMD RDNA 3 через кекст `PhxBringup.kext`.

---

## ⚠️ Известные проблемы и проверенное железо

* **Проверенное железо:** Ноутбук Firebat U6C (AMD Ryzen 7 7840HS / 8845HS, Radeon 780M GFX1103, матрица 2560x1600 120Hz eDP).
* **Непроверенные устройства:** Ноутбуки Framework 13 AMD, ASUS ROG Ally, GPD Win, линейки Lenovo ThinkPad / Yoga на базе Phoenix. Работоспособность на них не подтверждена.
* **Скорость вывода кадров:** Кадры `WindowServer` копируются в текущий фреймбуфер с помощью потоковых инструкций SSE/MOVNTI, обеспечивая около 41 FPS на встроенном дисплее.
* **Внешние экраны:** Горячее подключение и звук по HDMI и USB-C DisplayPort Alt Mode находятся в экспериментальной разработке.
* **Режим сна (Sleep / Wake):** Управление питанием при переходе в сон и пробуждении полноценно не тестировалось и может приводить к сбоям подсветки или экрана.
* **Экран входа (Loginwindow):** При первой загрузке фон экрана входа может оставаться темным до авторизации. Рекомендуется включить **Автоматический вход** в настройках пользователей macOS.

---

## 🛠️ Сборка из исходников и воспроизводимость

### Требования
* **macOS:** Xcode 15+ или Command Line Tools с Apple Clang.
* **Linux / Кросс-компиляция:** Clang 15+, Make, Python 3.9+.
* **Заголовки ядра:** Репозиторий [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) для компиляции кекстов.

### Команды сборки

```bash
# Загрузка прошивок Phoenix из linux-firmware (нужны для тестов ucode)
make fixtures

# Сборка и запуск юнит-тестов (Linux или macOS)
make test

# Сборка PhxBringup.kext в macOS
sh src/phxbringup/build.sh /path/to/MacKernelSDK

# Сборка PhxAccel.kext в macOS
sh src/phxaccel/build.sh /path/to/MacKernelSDK
```

Непрерывная интеграция автоматически проверяет компиляцию юнит-тестов при каждом коммите через [GitHub Actions](.github/workflows/ci.yml).

---

## 🏛️ Архитектура драйвера

```
┌────────────────────────────────────────────────────────────────────────┐
│                   Пользовательские приложения macOS                    │
│     (Finder, Safari, Metal-приложения, композитор WindowServer)        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Metal API
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  /Library/GPUBundles/NVMTLDriver.bundle (Транслятор Metal-поверх-Vulkan)│
│  └── /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib (Mesa RADV 1.4)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ IOKit UserClient ABI
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│  Кексты ядра macOS (IOKit / Mach-O)                                    │
│  ├── PhxBringup.kext (Build 29 в OpenCore EFI)                         │
│  │   ├── Загрузчик прошивок PSP 13.0.4 и управление кольцами           │
│  │   ├── Планировщик MES v11 и отправка в очередь KIQ                  │
│  │   └── Выделение 4-уровневых таблиц GPUVM на процесс (VMID 8..15)    │
│  └── PhxAccel.kext (Вспомогательная коллекция Auxiliary Kernel)        │
│      ├── PhxAMDFB (Фреймбуфер IOFramebuffer с тегированием AMD)        │
│      ├── PhxAGDC (Согласование дисплея AppleGraphicsDeviceControl)     │
│      └── NMXAccel / NVDisplayPipe (Потоковый вывод кадров)             │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ MMIO BAR0 / Doorbell BAR2
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│         Кремний AMD Radeon 780M (Hawk Point / Phoenix GFX1103)         │
│          Матрица DCN 3.1.4 (2560x1600 @ 120Hz eDP)                     │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 📄 Лицензии и благодарности

Репозиторий распространяется под несколькими лицензиями в зависимости от компонента. Полный текст приведен в файле [LICENSE](LICENSE).

* **PhxBringup.kext и утилиты (`src/phxbringup`, `tools/phxctl`):** Распространяются под лицензией **GPL-2.0-only** или MIT, отражая совместимость с кодом Linux `amdgpu`.
* **PhxAccel и патчи NVMTLDriver (`src/phxaccel`, `tools/nvmtl-patches`):** Основаны на разработках NullMoth и распространяются под некоммерческой лицензией **PolyForm Noncommercial License 1.0.0**.
  ```
  NullMoth NVIDIA driver for macOS
  Copyright (c) 2026 NullMoth Systems.
  SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
  ```
  *Коммерческое использование компонентов под данной лицензией строго запрещено.*
* **Патчи Mesa (`mesa-patches-phoenix`):** Распространяются под оригинальной лицензией **MIT License** с сохранением копирайтов авторов Mesa.
* **Благодарности:** Команде Linux `amdgpu`, разработчикам проекта Mesa 3D, сообществам [Acidanthera](https://github.com/acidanthera), [Cobalt64](https://cobalt64.com) и AMD-OSX.
