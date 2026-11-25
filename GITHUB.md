# Готовые тексты для GitHub

Скопируйте нужный раздел в соответствующие поля репозитория.

---

## 1. Описание репозитория (About / Repository description)

```
Графическая ОС с нуля: 16-битный загрузчик + 32-битное ядро, VESA 1024x768, окна, мышь, интернет-браузер, планировщик задач, текстовый редактор, игра SNAKE и терминал с настоящими RPM-пакетами, pip и git. Загружается в QEMU.
```

Короткий вариант (до 100 символов):

```
ОС для QEMU: окна, браузер, apt/pip, задачи, SNAKE
```

## 2. Topics (теги)

```
operating-system osdev bootloader kernel qemu bare-metal vesa graphics browser assembly c tiny-os hobby-os
```

## 3. Commit message

```
Add bootable ISO and working internet browser

- Build El Torito no-emulation ISO with a CD-specific bootloader that
  copies the already-loaded kernel instead of calling int 13h (drive 0xE0)
- Add TINY BROWSER with URL bar, history, links, scrolling and compact
  HTML renderer; fetch real pages over COM1 via nethelper.py
- Fix mouse lag: redraw only two 12x16 rectangles on cursor movement
- Extend font to 83 glyphs, polish window chrome, icons and live RTC clock

gen0.2:
- Add start menu, real minimize, double-click maximize, click-to-raise
- Add 5 themes, CMOS date, mouse wheel, link hover
- Add HOME button, search queries, page title and status line
- Make URL bar editable: caret, arrows, Home/End, Delete, Ctrl+L and Ctrl+R
- Handle Shift for lowercase URLs; add scrollbar, line counter, hr/img/bullets
- Fix PS/2 mouse ID detection (4-byte wheel packets), switch search to Bing RSS
- Add forward history, F6/F7 and Ctrl+Left/Right, Shift punctuation, strip JS/CSS
- Major graphics: starry landscape wallpaper, sun, mountains, beveled buttons,
  pixel app icons, bold/underlined headings, menu footer, F8 to toggle menu
- Add window resizing, Alt+Tab, Ctrl+W and focus hand-off after close
- Add keyboard link navigation (Tab/Shift+Tab/Enter/Esc) and LOADING status
- Render numbered lists, preformatted blocks, blockquotes and inline code
- Make Notes multi-line; show date and page statistics in System Info
- Add TINY SHELL (F9) with pip/pkg package manager backed by PyPI
- Use PIT-timed COM1 reads so slow pages and package lookups load reliably

gen0.3:
- Add a virtual filesystem and shell commands: pwd, ls, cd, mkdir, cat
- Add git clone/status/log executed by nethelper on the host
- Make pip install real: install into site-packages, add pip show and version specs
- Add terminal command history with Up/Down keys

gen0.5:
- Add a cooperative task scheduler with a task manager window (F11) and
  ps/kill/resume/top commands; tasks are kernel routines, not Linux processes
- Add a Snake game (F12) driven by the scheduler, with score and best score
- Add three PC speaker melodies with play/stop commands
- Add a path-aware text editor: edit FILE, Ctrl+S writes back to that file
- Add file commands touch, mv, cp, rm, du, tree, head, tail, wc
- Add dmesg kernel log, free and df system utilities
- Fix PIT timeout arithmetic, boot large kernels in 32-sector LBA chunks,
  zero the kernel BSS and make the ISO copy overlap-safe
- Enlarge the taskbar to eight windows and fix the window/menu hover IDs

gen0.4:
- Install real system packages: dnf download --resolve, rpm2cpio/cpio into
  tinyos_fs, apt_installed.json registry, apt update/search/info/install/
  remove/list/files/upgrade/clean and which
- Run installed programs by name inside bwrap with an overlay /usr,
  isolated network/PID namespaces and a timeout
- Add a calculator window (F10) with mouse and keyboard input
- Add an IDT with 32 exception handlers and a KERNEL PANIC screen showing
  vector, error code and EIP; add panic and beep commands
- Detect the CPU vendor via CPUID and show uptime in System Info
- Enlarge terminal replies to 4 KiB, add uptime/uname/history commands
```

Короткий вариант:

```
Add bootable ISO, internet browser and lag-free mouse rendering
```

## 4. Release notes (для Releases)

~~~markdown
## TINY OS gen0.5

Графическая ОС, которую можно загрузить в QEMU.
Образ диска занимает 67 072 байт, ISO — 462 848 байт.

### Что нового
- **Кооперативные задачи**: планировщик на таймере PIT выполняет фоновые
  задачи (часы, звук, игра). Окно **TASK MANAGER (F11)** показывает id,
  состояние, счётчики и полосы активности. Команды `ps`, `kill N`,
  `resume N`, `top` работают из терминала. Это задачи ядра, а не процессы
  Linux.
- **SNAKE (F12)**: игра со змейкой, управление стрелками, Space — старт и
  рестарт, счёт и рекорд в окне. Движение змейки — отдельная задача
  планировщика.
- **Звук**: три коротких мелодии через `play 0`, `play 1`, `play 2` и
  `stop`; звук выводится через PC speaker (канал 2 PIT).
- **Текстовый редактор**: `edit FILE` загружает файл в окно редактора,
  Ctrl+S сохраняет его по тому же пути, Ctrl+O перечитывает с диска.
- **Файлы**: `touch`, `mv`, `cp`, `rm`, `du`, `tree`, `head`, `tail`, `wc`
  работают с виртуальной ФС `tinyos_fs` на хосте.
- **Системные утилиты**: `dmesg` (журнал ядра), `free` (память по CMOS),
  `df`, `du`.
- **Исправления загрузки**: корректные интервалы PIT, загрузка больших
  ядер порциями по 32 сектора через LBA, очистка BSS и безопасное
  копирование ядра с ISO при перекрытии исходного и целевого участков.
- **Интерфейс**: панель задач на 8 окон, новый пункт меню, исправлены
  конфликты hover-идентификаторов.

### Файлы
- `os.img` — загрузочный raw-образ диска (67 072 байт)
- `tinyos.iso` — загрузочный ISO (El Torito, no emulation, 462 848 байт)
- `run.sh` — запуск с интернетом (поднимает `nethelper.py`)

### Запуск
```bash
./run.sh                                             # с интернетом
qemu-system-i386 -cdrom tinyos.iso -boot d -m 64     # с ISO
```

### Управление
OS или F8 — меню, F1–F3 — окна, F4 — тема, F5 — браузер/обновить,
F9 — терминал, F10 — калькулятор, F11 — задачи, F12 — SNAKE,
ESC — стартовая страница, ENTER — адрес/домен/поиск или открыть ссылку,
Ctrl+L — новая адресная строка, Ctrl+R — обновить, F6/F7 — история,
Alt+Tab — переключение окон, Ctrl+W — закрыть окно,
в Notes Ctrl+S/Ctrl+O — сохранить/загрузить файл,
в терминале — `help`, `apt install NAME`, `pip install NAME`,
`ps`, `kill N`, `play 0`, `edit FILE`, `dmesg`, `beep`, `panic`.
~~~

## 5. Заголовок README (первая строка)

```
# TINY OS — графическая ОС с настоящим интернет-браузером
```

## 6. Шаблон issue (баг-репорт)

```
**Окружение:** QEMU <версия>, хост <ОС>, образ `os.img` / `tinyos.iso`
**Шаги:** ./run.sh → F5 → ввести URL → ENTER
**Ожидалось:** страница отображается
**Фактически:** ...
**Примечание:** без `run.sh` браузер показывает OFFLINE — это ожидаемо,
нужен хост-помощник `nethelper.py`.
```

---

## 7. Пошаговый план: создать репозиторий и загрузить всё на GitHub

### Шаг 1. Подготовьте папку

Откройте терминал и перейдите в папку проекта (путь с кириллицей берите в кавычках):

```bash
cd "/home/ghost/Документы/Проект по умолчанию/tinyos"
ls -la
```

Убедитесь, что на месте `README.md`, `build.sh`, `kernel.c`, `boot.S`, `os.img`, `tinyos.iso`.

### Шаг 2. Инициализируйте git

```bash
git init -b main
git config user.name  "Ваш логин GitHub"
git config user.email "ваша-почта@example.com"
```

### Шаг 3. Отберите файлы

Создайте `.gitignore`, чтобы не загружать мусор сборки:

```bash
cat > .gitignore <<'EOF'
*.o
*.elf
*.bin
os-cd.img
.iso-root/
__pycache__/
EOF
```

> `os.img` и `tinyos.iso` **не** попадают под игнор — они нужны как готовые образы.
> `font.h` можно загрузить (он генерируется, но нужен для сборки без генератора).

Проверьте, что именно попадёт в коммит:

```bash
git add .
git status
```

Должны быть: `README.md`, `GITHUB.md`, `boot.S`, `boot.ld`, `kernel.c`, `kernel.ld`,
`font.h`, `tools/genfont.py`, `nethelper.py`, `run.sh`, `build.sh`, `os.img`,
`tinyos.iso`, `screenshot.png`, `.gitignore`.

### Шаг 4. Сделайте коммит

```bash
git commit -m "Add bootable ISO, internet browser and lag-free mouse rendering"
```

(Можно взять полный текст коммита из раздела 3 выше.)

### Шаг 5. Создайте репозиторий на GitHub

**Вариант А — через сайт (без дополнительных программ):**

1. Зайдите на <https://github.com/new>.
2. Repository name: `tiny-os` (или `tinyos`).
3. Description: вставьте текст из раздела 1 выше.
4. Выберите **Public**.
5. **Не** ставьте галочки «Add a README», «.gitignore», «license» — файлы уже есть локально.
6. Нажмите **Create repository**.
7. На странице репозитория нажмите зелёную кнопку **Code**, выберите **HTTPS** и скопируйте ссылку вида
   `https://github.com/<логин>/tiny-os.git`.

**Вариант Б — через GitHub CLI** (если установите `gh` и выполните `gh auth login`):

```bash
gh repo create tiny-os --public --source=. --remote=origin --push \
  --description "ОС для QEMU: окна, браузер, apt/pip, задачи, SNAKE"
```

### Шаг 6. Привяжите удалённый репозиторий и залейте код

Для варианта А (подставьте свою ссылку):

```bash
git remote add origin https://github.com/<логин>/tiny-os.git
git branch -M main
git push -u origin main
```

При первом push GitHub попросит логин и **Personal Access Token** (не пароль).
Токен создаётся тут: <https://github.com/settings/tokens> → **Generate new token (classic)** →
галочка `repo` → скопировать и вставить вместо пароля.

### Шаг 7. Оформите страницу репозитория

В репозитории: **About → ⚙️** (шестерёнка):

- Description — текст из раздела 1;
- Topics — список из раздела 2;
- Website — можно оставить пустым;
- поставьте галочку **Readme**.

### Шаг 8. Создайте Release и приложите образы

1. Нажмите **Releases → Create a new release**.
2. Tag: `gen0.5`, Title: `TINY OS gen0.5`.
3. В поле описания вставьте release notes из раздела 4.
4. Перетащите вниз в блок **Attach binaries**: `tinyos.iso`, `os.img`, `screenshot.png`.
5. Нажмите **Publish release**.

### Шаг 9. Проверьте результат

Обновите страницу репозитория: README должен отображаться с названием **TINY OS**,
в Release должны лежать `tinyos.iso` и `os.img`. Проверьте скачивание ISO и его запуск:

```bash
qemu-system-i386 -cdrom tinyos.iso -boot d -m 64 -k en-us
```

### Полезные команды потом

```bash
git status                 # что изменилось
git add . && git commit -m "описание" && git push   # залить правки
git remote -v              # посмотреть ссылку на репозиторий
git log --oneline          # история коммитов
```

### Если передумали или ошиблись

- Убрать файл из репозитория: `git rm --cached имя && git commit -m "..." && git push`.
- Переименовать репозиторий: **Settings → Repository name** — GitHub сам сделает редирект.
- Удалить репозиторий: **Settings → Danger Zone → Delete this repository**.
