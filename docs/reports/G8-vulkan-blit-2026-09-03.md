# G8 — регрессия blit-пути Vulkan: заливка панели стирала удержанный кадр

- **Задача:** `G8`
- **Дата:** 2026-09-03
- **Ветка:** `fix/G8-vulkan-blit`
- **Разбираемый прогон:** `KiboMibo/shitty`, run `33692909258`
- **Правка:** `lib/shitty/render_vk.cpp`, одна строка условия и одна строка вычисления
- **Коммит-источник:** `eed01bbb` — **назван бисектой**, не рассуждением

## Одной строкой

Проход заливки панели, добавленный волной F9, выполнялся на **каждом** кадре,
а бэкенд Vulkan перерисовывает только строки, названные журналом
повреждений. Заливка красила фоном всю панель, поверх неё ложились только
свежие строки, а всё удержанное с прошлых кадров оказывалось стёрто. На
прямом пути это не видно: там свопчейн крутит три картинки, каждая из
которых устарела, и кадр всё равно выходит полным. На blit-пути картинка
одна, она несёт изображение вперёд — и уже второй `present` сессии
инкрементальный.

---

## 1. Воспроизведение

### 1.1. Среда

Nix-джобы CI гоняют Vulkan через `lavapipe`, значит GPU не нужен. Машина
arm64/macOS, воспроизведение — в Docker.

Первая попытка, `alpine:3.24` (как в джобе `Tests Alpine`), **не годится на
arm64** по двум независимым причинам, обе не имеют отношения к предмету:

1. С `gcc` бинарь падает в `SIGSEGV` внутри `ContextImpl::swapContext`
   (`ext/libstd/std/thr/context.cpp`). Функция объявлена `naked`, её тело —
   базовый `__asm__`, в котором параметры не упомянуты; GCC на aarch64 решает,
   что аргументы не нужны, и в `x1` приезжает ноль. Проверено в отладчике:
   `x1 = 0` на инструкции `ldr x2, [x1]`. С `clang` не воспроизводится.
2. С `clang` бинарь живёт, но падает уже `lavapipe` — в потоке `libvulkan_lvp`
   внутри `llvm::MCJIT`. Это переполнение стандартного 128-килобайтного
   стека потока musl.

Рабочая среда — `fedora:44` на arm64 (glibc), тот же список пакетов, что в
джобе `Tests Fedora`, плюс `clang` (по причине 1) и `dejavu-sans-mono-fonts`.

```
docker run -d --platform linux/arm64 --name g8-fedora --cpus=8 --memory=16g \
    -v <клон>:/src -w /src fedora:44 sleep infinity
dnf -y --disablerepo=fedora-cisco-openh264 --setopt=install_weak_deps=False \
    install clang gcc-c++ make git python3 perl pkgconf-pkg-config diffutils \
    procps-ng ragel glslang librsvg2-tools ncurses-devel freetype-devel \
    fontconfig-devel harfbuzz-devel libxkbcommon-devel vulkan-headers \
    vulkan-loader-devel mesa-vulkan-drivers wayland-devel \
    wayland-protocols-devel dejavu-sans-mono-fonts gdb
CC=clang CXX=clang++ ./build st_test
```

Прогон:

```
cd /src/tst
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.aarch64.json
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export SHITTY_TEST_BINARY=/src/.build/st_test SHITTY_TEST_PLATFORM=wayland
export SHITTY_TEST_FONTCONFIG=1 SHITTY_TEST_VULKAN_REQUIRED=1
python3 -m unittest -v test_gpu_smoke
```

### 1.2. Отказ до правки — те же два теста и те же два числа

```
test_font_change_replaces_gpu_resources (…GpuSmokeTest…) ... ok
test_plain_frames_present (…GpuSmokeTest…) ... ok
test_repaint_without_terminal_update_survives_repeated_readback (…GpuSmokeTest…) ... ok
test_resize_during_retained_update_rebuilds_frame (…GpuSmokeTest…) ... ok
test_resize_storm_leaves_a_matching_frame (…GpuSmokeTest…) ... ok
test_uncovered_script_flood_presents (…GpuSmokeTest…) ... ok
test_font_change_replaces_gpu_resources (…VulkanBlitSmokeTest…) ... FAIL
test_plain_frames_present (…VulkanBlitSmokeTest…) ... ok
test_repaint_without_terminal_update_survives_repeated_readback (…VulkanBlitSmokeTest…) ... ok
test_resize_during_retained_update_rebuilds_frame (…VulkanBlitSmokeTest…) ... FAIL
test_resize_storm_leaves_a_matching_frame (…VulkanBlitSmokeTest…) ... ok
test_uncovered_script_flood_presents (…VulkanBlitSmokeTest…) ... ok
AssertionError: rendered images differ by 255, tolerance 3
AssertionError: rendered images differ by 179, tolerance 3
Ran 12 tests in 0.918s
FAILED (failures=2)
EXIT=0
```

Совпадение с CI полное: два отказа, ровно те же тесты, ровно те же числа
`255` и `179`, прямой путь целиком зелёный.

### 1.3. После правки

```
test_font_change_replaces_gpu_resources (…GpuSmokeTest…) ... ok
test_plain_frames_present (…GpuSmokeTest…) ... ok
test_repaint_without_terminal_update_survives_repeated_readback (…GpuSmokeTest…) ... ok
test_resize_during_retained_update_rebuilds_frame (…GpuSmokeTest…) ... ok
test_resize_storm_leaves_a_matching_frame (…GpuSmokeTest…) ... ok
test_uncovered_script_flood_presents (…GpuSmokeTest…) ... ok
test_font_change_replaces_gpu_resources (…VulkanBlitSmokeTest…) ... ok
test_plain_frames_present (…VulkanBlitSmokeTest…) ... ok
test_repaint_without_terminal_update_survives_repeated_readback (…VulkanBlitSmokeTest…) ... ok
test_resize_during_retained_update_rebuilds_frame (…VulkanBlitSmokeTest…) ... ok
test_resize_storm_leaves_a_matching_frame (…VulkanBlitSmokeTest…) ... ok
test_uncovered_script_flood_presents (…VulkanBlitSmokeTest…) ... ok
Ran 12 tests in 0.852s
OK
EXIT=0
```

---

## 2. Локализация до коммита

### 2.1. Первая бисекта провалилась — и это диагноз, а не помеха

`git bisect start 73364d73 4996dad7~1` по всей истории: **154 шага `skip`**.
Причина — гейт `Build Linux` стоял красным с 20 августа, и внутри волны нет ни
одного коммита, который на Linux собирается. Сужение по путям
(`-- lib/shitty/render_vk.cpp lib/shitty/render.comp`) свело кандидатов к
тринадцати коммитам волны и упёрлось в то же:

```
There are only 'skip'ped commits left to test.
The first 'bad' commit could be any of:
159a343e 912160c8 30963130 a5a31d38 3a18e83c ee90fdda d014fa9e
eed01bbb 854e1246 ca3997c6 8325f312 f8b9b009 360e9351
We cannot bisect more!
```

### 2.2. Вторая бисекта — с починкой сборки в рабочем дереве

Все три поломки сборки внутри волны — компиляционные и линковочные, ни одна из
них не влияет на то, что рисуется. Каждую волна позже починила сама. Пробник
бисекты чинит их в рабочем дереве перед сборкой:

| Поломка | Чем чинится в пробнике | Чинил коммит |
|---|---|---|
| `static_assert(sizeof(PushConstants) == 116)` не обновлён после четырёх новых полей | assert приводится к тождественному | `912160c8` |
| `raiseError(u8"…", count, u8"…")` с сырыми массивами — неопределённая ссылка `stl::output` | оборачивается в `StringView` | `360e9351` |
| `SeamBands seams;` читается, а типа в дереве нет | тип объявляется рядом с `GpuCellUpdate` | `d014fa9e` |

Пробник, кроме того, фиксирует `tst/` на состоянии головы ветки (`git checkout
73364d73 -- tst/`): при движении по истории `test_gpu_smoke.py` меняется сам, а
измерять надо рендерер. Целевой тест —
`VulkanBlitSmokeTest.test_resize_during_retained_update_rebuilds_frame`, тело
которого совпадает с апстримным.

Вердикт:

```
git bisect start '73364d73' '4996dad7~1' '--' 'lib/shitty/render_vk.cpp' 'lib/shitty/render.comp'
git bisect bad  912160c85439444991a0b6a8b81493c92107f55b
git bisect good ca3997c6a23018bad77d478ef7615ec434d4f627
git bisect bad  3a18e83cec7e1375d4e3eb959c333d83ac152d50
git bisect bad  eed01bbb83b88d77f0883a80d17bc522b16910f0
bisect found first 'bad' commit
```

**`eed01bbb`** — «WIP RED: F9 divider painting — the one-pixel default moves
every pane boundary», 2026-08-21 15:11. Именно он вносит в
`recordCommands()` безусловный `vkCmdDispatch` заливки. Это же говорит
независимая проверка по содержимому:

```
$ git log --oneline -S 'vkCmdDispatch(frame.commandBuffer, ((u32)(paneWidth * paneHeight) + 63) / 64, 1, 1)' -- lib/shitty/render_vk.cpp
eed01bbb WIP RED: F9 divider painting - the one-pixel default moves every pane boundary
$ git log --oneline -S 'uint fillRow = pixelIndex / paneWidth' -- lib/shitty/render.comp
eed01bbb WIP RED: F9 divider painting - the one-pixel default moves every pane boundary
```

Соседний `ca3997c6` (его непосредственный предок среди коммитов, трогающих эти
файлы) собран тем же пробником и **зелёный**.

---

## 3. Механизм

### 3.1. Что делает бэкенд

`RendererImpl` хранит ячейки между кадрами и рисует не всю сетку, а строки,
которые назвал журнал повреждений. `materializeUpdates()` спрашивает
`damage.requiresFull(appliedGeneration, initialized)`: если да — выдаёт все
строки, если нет — только строки записей журнала новее применённого поколения.
Что «применено», зависит от пути:

- **прямой путь:** `chain->generations[imageIndex]` — у каждой картинки
  свопчейна своё поколение;
- **blit-путь:** `chain->outputGeneration` — одна offscreen-картинка на всех.

### 3.2. Что добавила волна

`eed01bbb` поставил перед проходом ячеек второй `vkCmdDispatch` — проход
заливки, который красит **весь прямоугольник панели** её фоном
(`storeAtAlpha` в ветке `fillPassBit` шейдера). Условие у него было только
геометрическое:

```cpp
if (paneWidth != 0 && paneHeight != 0) {
```

То есть заливка шла на каждом кадре, где есть хоть одно обновление ячейки. На
инкрементальном кадре она закрашивает фоном всю панель, а поверх ложатся
только свежие строки. Всё, что бэкенд удерживал с прошлых кадров, стирается
проходом, который задуман лежать **под** ячейками.

### 3.3. Почему падает только blit-путь

На прямом пути `imageCount = minImageCount + 1`, картинки чередуются, и та, в
которую пишет кадр, почти всегда либо ещё `!initialized`, либо несёт старое
поколение — `requiresFull` возвращает `true`, приходят все строки, и стирать
нечего. На blit-пути картинка одна и после первого же кадра она
`outputInitialized` с текущим поколением — второй `present` уже
инкрементальный, и дефект виден сразу.

Отсюда же ответ, почему падают ровно два теста из шести: только они делают
второй `present` после того, как первый уже нарисовал что-то, что должно
пережить кадр. `test_plain_frames_present` презентует один раз;
`test_uncovered_script_flood_presents` заканчивается `\x1b[2J` — полное
повреждение; `test_resize_storm_leaves_a_matching_frame` меняет форму сетки, а
это `fullDamage()`; `test_repaint_…` сравнивает GPU с GPU.

### 3.4. Что именно оказалось в картинке — построчно

Прямое измерение обеих картинок до правки (терминальные строки, а не пиксельные):

**`test_font_change_replaces_gpu_resources`**, картинка `204x88`,
`worst=255`, расходятся `2592` субпикселя из `53856`:

```
row  6..22 : worst=255  ink_ref=36..243  ink_gpu=0  gpu_uniform=True
row 23..43 : worst=  0  ink_ref=30..108  ink_gpu=30..108  gpu_uniform=False
```

После увеличения кегля колонок стало меньше, «before font change after»
перенеслось на две терминальные строки, последний `present` повредил только
вторую — и первая (пиксельные ряды 6..22) в GPU-картинке **равномерно залита
фоном**, чернил в ней нет вовсе, при том что в эталоне они есть. `255` — это
белый пиксель глифа против чёрного фона.

**`test_resize_during_retained_update_rebuilds_frame`**, картинка `44x52`,
`worst=179`, расходятся `1080` субпикселей из `6864`:

```
row  3..16 : worst=179  ink_ref=30..90  ink_gpu=0  gpu_uniform=True
row 19..32 : worst=179  ink_ref=30..90  ink_gpu=0  gpu_uniform=True
row 34..49 : worst=  0  ink_ref=24..78  ink_gpu=24..78  gpu_uniform=False
```

Картина та же: две терминальные строки стёрты, третья совпадает точно. Разница
между `255` и `179` — не разные дефекты, а разная яркость самого яркого
потерянного пикселя: там, где потеряна сплошная белая заливка глифа, максимум
`255`, там, где сглаженный штрих — `179`. Один дефект, два проявления.

После правки то же измерение даёт `worst=0`, `differing_subpixels=0` на обеих
картинках.

### 3.5. Почему Metal этим не болеет

`MetalRendererImpl::draw()` начинает каждый кадр с `MTLLoadActionClear` по
всей текстуре, а `buildPaneUpdates()` выдаёт **все** ячейки каждой панели
каждый кадр — удержанного изображения там нет вовсе. Поэтому те же два прохода
на Metal безусловны и безвредны, а здесь безусловными быть не могут. Тесты
Metal этого дефекта увидеть не могли.

---

## 4. Правка и почему именно она

`lib/shitty/render_vk.cpp`, три места:

1. `recordFrame()` спрашивает `damage.requiresFull(appliedGeneration,
   initialized)` — тот же вопрос, которым `materializeUpdates()` решает,
   выдавать ли все строки, — и передаёт ответ дальше как `fullRepaint`.
2. `recordCommands()` принимает его параметром.
3. Проход заливки получает это условие: `if (fullRepaint && paneWidth != 0 &&
   paneHeight != 0)`.

Заливка теперь идёт ровно тогда, когда поверх неё придут **все** ячейки,
которые её закрывают. Свойства, ради которых волна делалась, сохранены:

- `clearOutput ⇒ fullRepaint`. `clearDamageGeneration` присваивается только
  рядом с `fullDamage()` (`render_vk.cpp:2157` — обёртка поколения,
  `render_vk.cpp:2172` — смена формы или фона), поэтому
  `clearDamageGeneration ≤ fullGeneration` всегда. Кадр, на котором картинку
  чистят, — всегда полный, и отступы с фоном панели по-прежнему
  перекрашиваются.
- Смена фона панели (`OSC 11`) даёт `backgroundChanged` → `fullDamage()` →
  заливка идёт.
- Смена раскладки панелей меняет сетку → `shapeChanged` → `fullDamage()`.
- Смена геометрии окна пересоздаёт свопчейн → `!initialized` → `fullRepaint`.

Откат фичи не делается: проход заливки, поля `paneLeft`/`paneTop`,
`paneBackgroundAndFill`, альфа фона и швы — всё на месте, изменилось только
то, на каких кадрах заливка исполняется.

Проход **швов** оставлен безусловным намеренно. Полоса шва лежит в воздухе
между панелями, куда не пишет никто, кроме очистки и её самой; перерисовка
идемпотентна. Если бы шов зависел от `fullRepaint`, изменение цвета или
геометрии шва (`setSeams()` вызывается снаружи и повреждений не ставит) могло
бы остаться неотрисованным.

---

## 5. Таблица критериев

| # | Критерий | Статус | Вывод |
|---|---|---|---|
| 1 | Оба теста воспроизведены падающими до правки и проходят после | ✅ | §1.2 и §1.3, `EXIT=0` в обоих прогонах |
| 2 | Коммит-источник назван | ✅ | `eed01bbb`, бисектой; §2.2 |
| 3 | Механизм отказа описан | ✅ | §3, с построчным измерением обеих картинок |
| 4 | `./build st --clear` зелёная | ✅ | ниже |
| 5 | `./build unit_tests pty_test_helper` + `OK: 955` | ✅ | ниже |
| 6 | Остальные тесты Vulkan-пути не сломаны | ✅ | ниже |

### Критерий 4

```
$ ./build st --clear
[FD] {220/224} $(B)/font_data.h
[CC] {221/224} $(B)/obj/libshitty/lib/shitty/font_embedded.cpp.o
[CC] {222/224} $(B)/obj/libshitty/lib/shitty/parser.cpp.o
[AR] {223/224} $(B)/libshitty_prod.a
[LD] {224/224} $(B)/st
EXIT=0
```

224 узла.

### Критерий 5

Прогнан после `--clear`, то есть с нуля:

```
$ ./build unit_tests pty_test_helper
[CC] {123/125} $(B)/obj/libshitty_test/lib/shitty/parser.cpp.o
[AR] {124/125} $(B)/libshitty_test.a
[LD] {125/125} $(B)/unit_tests
BUILD_EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
OK: 955
RUN_EXIT=0
```

### Критерий 6

Прогнаны семь наборов, связанных с рендерингом:
`test_gpu_parity`, `test_render_contract`, `test_renderer_replacement`,
`test_soft_render`, `test_bitmap_font_render`, `test_color_font_render`,
`test_nerd_icon_render` — 20 тестов.

До правки:

```
Ran 20 tests in 0.535s
FAILED (errors=1)
RuntimeError: vulkan: this backend presents one pane per frame, not 2 - splits are not supported here yet
```

После правки:

```
Ran 20 tests in 0.539s
FAILED (errors=1)
RuntimeError: vulkan: this backend presents one pane per frame, not 2 - splits are not supported here yet
```

Один и тот же отказ до и после — `test_gpu_parity.SplitGpuParityTest`, предмет
задачи `G9`, к нему не притрагивался. Остальные 19 зелёные в обоих прогонах.

Плюс полный `test_gpu_smoke` (12 тестов) — §1.2 и §1.3.

---

## Обнаружено

Три находки, к предмету `G8` не относящиеся, но записанные, потому что кому-то
они сэкономят день.

**1. `swapContext` на aarch64 ломается под GCC.**
`ext/libstd/std/thr/context.cpp`: `ContextImpl::swapContext` объявлена
`__attribute__((naked, noinline))`, а её тело — базовый `__asm__`, в котором
параметры не упомянуты ни разу. GCC 15 на aarch64 из этого заключает, что
аргументы функции не нужны, и вызывающая сторона передаёт мусор: в отладчике
`x1 = 0` на `ldr x2, [x1]`, сразу SIGSEGV в первом же переключении фибера
(`SessionSet::create` → `SchedulerImpl::create`). Clang код генерирует
правильно, x86_64 не затронут (там свой блок), поэтому CI этого не видит.
Лечится либо явным упоминанием параметров в расширенном asm, либо
`-fno-ipa-sra` на файле. **Не проверял**, воспроизводится ли на aarch64 с
glibc — на Fedora я сразу перешёл на clang.

**2. `lavapipe` на musl падает в собственном JIT.**
С clang-сборкой на `alpine:3.24`/arm64 отказ переезжает в поток
`libvulkan_lvp` внутри `llvm::MCJIT::emitObject`. Это стандартный
128-килобайтный стек потока musl. Джоб `Tests Alpine` на x86_64 до Vulkan-тестов
не доходит (падает раньше на `SplitGpuParityTest`), так что для CI это пока
теория, но если он когда-нибудь дойдёт — искать надо здесь, а не в рендерере.

**3. Волна F9/R9/T10 не собиралась на Linux ни на одном своём коммите.**
Это установлено измерением, а не чтением: 154 `skip` на первой бисекте и 13 из
13 коммитов волны на второй. Три поломки перечислены в §2.2; все три —
следствие того, что `render_vk.cpp` правился без компилятора, который его
читает. Двух из них хватило бы одного локального кросс-прогона компилятора,
чтобы не дожить до CI.

---

## Что осталось непроверенным

Прямым текстом, потому что разница существенная.

1. **Правка не прогонялась на x86_64 и не прогонялась на Nix.** Воспроизведение
   и проверка сделаны на aarch64/Fedora/clang с `lavapipe`. Числа отказа до
   правки совпали с CI побайтово (`255` и `179`, те же два теста, тот же
   зелёный прямой путь), и это сильный довод, что среда эквивалентна, — но
   доводом, а не измерением, он и остаётся. Окончательное слово за CI.

2. **Полный `./build test` на Linux не прогонялся.** Прогнаны `test_gpu_smoke`
   (12) и семь связанных с рендерингом наборов (20). Остальная часть набора на
   Linux содержит известные отказы, к `G8` не относящиеся (отказ A из отчёта
   `G6` — гвард FreeType, `SplitGpuParityTest` — задача `G9`), и разбирать их
   здесь я не стал.

3. **Поведение при настоящем сплите не проверено ничем.** Этот бэкенд отказывает
   при `count > 1`, поэтому ни один тест не проходит через `seams.bands`
   непустым и ни один не рисует две панели. Рассуждение §4 о том, что
   `fullRepaint` покрывает все случаи, где заливка нужна, опирается на чтение
   `update()` и `fullDamage()`, а не на прогон. Когда сплиты на Vulkan
   заработают, это место надо перепроверить первым.

4. **Идемпотентность прохода швов не измерена.** Оставлен безусловным по
   рассуждению (никто, кроме очистки и его самого, в этот воздух не пишет);
   тестом это не подтверждено, потому что списка швов на этом бэкенде сегодня
   не бывает.

5. **`recordRepaintCommands()` (путь `repaint()` на blit) правкой не тронут** и
   тронут быть не должен: он только повторяет blit готовой картинки. Тест
   `test_repaint_without_terminal_update_survives_repeated_readback` зелёный до
   и после, но он сравнивает GPU с GPU и разошедшуюся с эталоном картинку не
   поймал бы.

6. **Находка 1 (GCC/aarch64) не заведена как задача и не чинилась.** Она вне
   границ `G8`; трогать `ext/libstd` в этой ветке я не стал.
