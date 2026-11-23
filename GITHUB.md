# Готовые тексты для GitHub

Скопируйте нужный раздел в соответствующие поля репозитория.

---

## 1. Описание репозитория (About / Repository description)

```
Графическая ОС с нуля: 16-битный загрузчик + 32-битное ядро, VESA 1024x768, окна, мышь, интернет-браузер, калькулятор и терминал с настоящей установкой RPM-пакетов, pip и git. Загружается в QEMU.
```

Короткий вариант (до 100 символов):

```
ОС для QEMU: окна, браузер, apt/pip, калькулятор
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
## TINY OS gen0.4

Графическая ОС, которую можно загрузить в QEMU.
Образ диска занимает 57 344 байт, ISO — 452 608 байт.

### Что нового
- **Настоящий менеджер системных пакетов** `apt` (алиасы `pkg`, `dnf`):
  `apt update`, `apt search`, `apt show`, `apt install`, `apt remove`,
  `apt list`, `apt files`, `apt upgrade`, `apt clean`. Хост скачивает RPM
  вместе с зависимостями через `dnf download --resolve`, пакет
  распаковывается в `tinyos_fs` через `rpm2cpio | cpio`, реестр
  `apt_installed.json` хранит версию, размер, архитектуру и файлы.
- **Запуск установленных программ по имени**: `apt install cowsay`, затем
  `cowsay HELLO`. Бинарник запускается в `bwrap`: `/usr` собран из
  хостового `/usr` и `tinyos_fs/usr`, сеть и PID изолированы, стоит
  таймаут. Проверено на cowsay, figlet, toilet и fortune.
- **Калькулятор (F10)**: отдельное окно, кнопки мышью и клавиатура,
  `+ - * /`, Enter, Backspace, C, защита от переполнения.
- **IDT и KERNEL PANIC**: 256 записей, 32 вектора исключений CPU,
  экран с номером исключения, кодом ошибки и EIP вместо тихой
  перезагрузки. Команда `panic` показывает его по `int 3`.
- **PC speaker**: `beep` играет два тона, паника подаёт сигнал.
- **CPUID и uptime**: производитель CPU и время работы в SYSTEM INFO,
  команды `uptime` и `uname`.
- **Терминал**: ответы до 4 КиБ, 24 строки на экран, длинные таймауты
  для apt/pip/git, строка прогресса до ожидания, команда `history`.
- Исправлен Shift+8: теперь `*`, а не `(`.
- `pip` остаётся отдельным менеджером для Python-пакетов.

### Файлы
- `os.img` — загрузочный raw-образ диска (57 344 байт)
- `tinyos.iso` — загрузочный ISO (El Torito, no emulation, 452 608 байт)
- `run.sh` — запуск с интернетом (поднимает `nethelper.py`)

### Запуск
```bash
./run.sh                                             # с интернетом
qemu-system-i386 -cdrom tinyos.iso -boot d -m 64     # с ISO
```

### Управление
OS или F8 — меню, F1–F3 — окна, F4 — тема, F5 — браузер/обновить,
F9 — терминал, F10 — калькулятор, ESC — стартовая страница,
ENTER — адрес/домен/поиск или открыть выбранную ссылку,
Ctrl+L — новая адресная строка, Ctrl+R — обновить, F6/F7 — история
назад/вперёд, ←/→ и Home/End — редактирование адреса, Shift+знаки —
`? & = + _ < > @ # % ! "`, ↑/↓ и PageUp/PageDown — прокрутка, колесо мыши —
прокрутка, Tab/Shift+Tab — выбор ссылки, Alt+Tab — переключение окон,
Ctrl+W — закрыть окно, в Notes Ctrl+S/Ctrl+O — сохранить/загрузить,
двойной клик по заголовку — развернуть окно,
правый нижний угол — изменить размер; в терминале — `help`, ↑/↓ — история,
PageUp/PageDown — прокрутка, `apt install NAME`, `pip install NAME`,
`which CMD`, `beep`, `calc`, `panic`.
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
  --description "ОС для QEMU: окна, браузер, apt/pip, калькулятор"
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
2. Tag: `gen0.4`, Title: `TINY OS gen0.4`.
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
