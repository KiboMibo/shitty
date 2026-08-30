# `T2` — падающий тест регрессии: смена экрана в панели вешает окно

**Дата:** 2026-08-29
**Ветка:** `feat/T2-alt-screen-regression` (основное рабочее дерево), от `12906595`
**План:** `docs/plans/2026-08-29-pane-frame-stall.md`, задача `T2`, волна 2
**Диагноз:** `docs/research/2026-08-29-tui-in-split-pane-diagnosis.md`, §3, §5, §7
**Скилл:** `/coding-test`, делегированный режим
**Статус:** `FAIL` — и это цель задачи, а не её провал

Написано **9 тестов**, `tst/test_pane_alt_screen.py`. На текущем `HEAD`
**7 красные, 2 зелёные**. Красные становятся зелёными после `T3` —
проверено сборкой с гипотезой исправления (§ «Чувствительность»).
Продуктовый код не тронут: `git diff` по `lib/` пуст.

## Коммиты

| Коммит | Что |
|---|---|
| `0688ca6d` | `tst/test_pane_alt_screen.py` — восемь тестов, шесть красных |
| `bbb47746` | Девятый тест: экран меняет **сфокусированная** панель |
| `32a48045` | Этот отчёт |
| `e4dfe155` | Точность в комментарии девятого теста |

## Оракул: чем ловится «кадр не сел»

Оракул — **`REPAINT`**, а не таймер и не `sleep`. Цепочка целиком:

```
harness.repaint()  ->  REPAINT (test_mode.cpp:3012)
                   ->  TestTerminal::repaint(): window.requestFrame(); present()
                   ->  WindowHeadless::dispatchFrame()
                   ->  ApplicationImpl::frame() -> presentTerminal()
                   ->  bool, который вернул composer.renderer->update(...)
```

`present()` (`test_mode.cpp:1582`) возвращает `false`, если
`dispatchFrame()` вернул `false`; `REPAINT` на это отвечает
`ERR repaint failed`, и харнесс поднимает `RuntimeError`. То есть **один
вызов `repaint()` — это ровно один запрошенный кадр и один ответ
`presentTerminal()`**, наблюдаемый в питоне.

Отсюда оракул плана «кадр садится не позже второго вызова
`presentTerminal()`» выражается дословно:

1. `write_to_pane(0, ...)` — `feedPtyOutput()` сам делает
   `requestFrame(); present()`. Это **вызов №1**;
2. `expect_frame(terminal, ...)` — `repaint()`. Это **вызов №2**, и он
   обязан вернуть `true`.

**Почему это не зависит от тайминга.** Заголовочный вопрос «сколько
ждать» здесь не возникает вовсе: кадр не «приходит когда-нибудь», его
диспетчеризует сам тест синхронной командой протокола, и ответ приходит
в той же команде. Ни один тест в файле не спит, не опрашивает состояние
в цикле и не имеет понятия о времени. Ускорение или замедление машины не
может ни починить, ни сломать ни одного утверждения — считаются кадры, а
не миллисекунды.

Второй, независимый предохранитель от класса «зависание вместо отказа»
уже стоит в харнессе: `REPLY_TIMEOUT = 10` на сокете (`F1c`). Он не
участвует в оракуле — он гарантирует, что даже неизвестный отказ станет
красным тестом, а не подвисшей джобой.

**Почему падение внятное.** `expect_frame()` перехватывает
`RuntimeError` и переформулирует его:

```
AssertionError: the frame never settled after one pane entered the alternate
screen: the renderer refused it again on a frame the window asked for itself
(repaint failed). A refusal answered by requestFrame() and nothing else comes
back the same way every time, so this window is stalled and not slow - it
will not present again while the split lives.
```

В сообщении названы: что происходило, что именно отказало и почему это
не «медленно», а «навсегда». Голый таймаут описал бы дефект не лучше,
чем исходный баг-репорт.

## Состав набора

### Красные на `HEAD` (7)

| Тест | Что утверждает |
|---|---|
| `test_a_pane_entering_the_alternate_screen_lets_the_frame_settle` | Основной сценарий: панель 0 из двух уходит в `\033[?1049h`, панель 1 молчит. Плюс `screen_text_pane(1)` по-прежнему отдаёт удержанное содержимое соседки |
| `test_the_focused_pane_entering_the_alternate_screen_settles_too` | То же, но экран меняет **сфокусированная** панель — расстановка из баг-репорта |
| `test_a_pane_leaving_the_alternate_screen_lets_the_frame_settle` | Зеркальный случай `\033[?1049l`, отдельным тестом |
| `test_the_legacy_alternate_screen_sequence_stalls_the_window_too` | `\033[?47h` — другая последовательность, другая ветка парсера |
| `test_rebuilding_the_alternate_screen_in_place_stalls_the_window_too` | `\033[?1049h`, посланный панели, **уже находящейся** на альт-экране |
| `test_a_stalled_window_does_not_come_back_on_its_own` | Пять кадров подряд после смены экрана — ни один не отвергнут |
| `test_a_third_pane_makes_no_difference` | Три панели, говорит одна |

### Зелёные на `HEAD` и после `T3` (2)

| Тест | Зачем |
|---|---|
| `test_a_lone_pane_changing_screens_settles` | Одна панель — кадр садится. Дефект в числе панелей, а не в смене экрана |
| `test_a_frame_no_pane_is_silent_in_settles` | Кадр, в котором повреждены обе панели, садится. Отказ — про молчащую соседку, а не про смену экрана |

Контроли здесь не для процентов. Красные тесты обвиняют конкретный
механизм; если бы обвиняемые детали ломались сами по себе, обвинение
ничего не стоило бы. Исправление, которое сделало бы красные зелёными,
уронив контроли (например «больше никогда не пересобирать форму»), этими
двумя тестами отсекается.

## Вывод прогона на текущем `HEAD` (`0688ca6d`)

```
$ cd tst && SHITTY_TEST_BINARY="$(readlink -f ../.build/st_test)" \
      python3 -m unittest test_pane_alt_screen -v

test_a_pane_entering_the_alternate_screen_lets_the_frame_settle ... FAIL
test_a_pane_leaving_the_alternate_screen_lets_the_frame_settle ... FAIL
test_a_stalled_window_does_not_come_back_on_its_own ... FAIL
test_a_third_pane_makes_no_difference ... FAIL
test_rebuilding_the_alternate_screen_in_place_stalls_the_window_too ... FAIL
test_the_focused_pane_entering_the_alternate_screen_settles_too ... FAIL
test_the_legacy_alternate_screen_sequence_stalls_the_window_too ... FAIL
test_a_frame_no_pane_is_silent_in_settles ... ok
test_a_lone_pane_changing_screens_settles ... ok

======================================================================
FAIL: test_a_pane_entering_the_alternate_screen_lets_the_frame_settle
----------------------------------------------------------------------
Traceback (most recent call last):
  File "tst/test_pane_alt_screen.py", line 120, in
      test_a_pane_entering_the_alternate_screen_lets_the_frame_settle
    expect_frame(terminal, "one pane entered the alternate screen")
  File "tst/test_pane_alt_screen.py", line 75, in expect_frame
    raise AssertionError(
AssertionError: the frame never settled after one pane entered the alternate
screen: the renderer refused it again on a frame the window asked for itself
(repaint failed). A refusal answered by requestFrame() and nothing else comes
back the same way every time, so this window is stalled and not slow - it
will not present again while the split lives.

======================================================================
FAIL: test_a_pane_leaving_the_alternate_screen_lets_the_frame_settle
----------------------------------------------------------------------
    expect_frame(terminal, "one pane left the alternate screen")
AssertionError: the frame never settled after one pane left the alternate
screen: ... (repaint failed). ...

======================================================================
FAIL: test_a_stalled_window_does_not_come_back_on_its_own
----------------------------------------------------------------------
    expect_frame(terminal, f"the screen change, frame {attempt + 1}")
AssertionError: the frame never settled after the screen change, frame 1:
... (repaint failed). ...

======================================================================
FAIL: test_a_third_pane_makes_no_difference
----------------------------------------------------------------------
    expect_frame(terminal, "one pane of three entered the alternate screen")
AssertionError: the frame never settled after one pane of three entered the
alternate screen: ... (repaint failed). ...

======================================================================
FAIL: test_rebuilding_the_alternate_screen_in_place_stalls_the_window_too
----------------------------------------------------------------------
    expect_frame(terminal, "the alternate screen was rebuilt in place")
AssertionError: the frame never settled after the alternate screen was
rebuilt in place: ... (repaint failed). ...

======================================================================
FAIL: test_the_focused_pane_entering_the_alternate_screen_settles_too
----------------------------------------------------------------------
    expect_frame(terminal, "the focused pane entered the alternate screen")
AssertionError: the frame never settled after the focused pane entered the
alternate screen: ... (repaint failed). ...

======================================================================
FAIL: test_the_legacy_alternate_screen_sequence_stalls_the_window_too
----------------------------------------------------------------------
    expect_frame(terminal, "one pane switched screens with ?47h")
AssertionError: the frame never settled after one pane switched screens with
?47h: ... (repaint failed). ...

----------------------------------------------------------------------
Ran 9 tests in 0.081s

FAILED (failures=7)
```

(Повторяющиеся хвосты сообщений сокращены многоточием — текст в каждом
случае один и тот же, полный вариант приведён выше в §«Оракул».)

**Устойчивость.** Три подряд идущих прогона — `FAILED (failures=7)`
каждый; прогон в случайном порядке методов — тот же результат.
Мигания нет: тесты не делят ни состояния, ни процесса, каждый поднимает
свой `st_test`.

## Что видно рядом с падением

Прямая иллюстрация к «TUI запускается, стоит окно» — снято тем же
харнессом (probe в scratchpad'е сессии, в `tst/` не кладу):

```
--- перед сменой экрана
    pane 0: ['PLAIN0']          <- модель панели 0
    pane 1: ['PLAIN1']          <- модель панели 1
    renderer retained: ['PLAIN1']
write pane0 1049h                -> OK
repaint after 1049h (1)          -> RuntimeError: repaint failed
repaint after 1049h (2)          -> RuntimeError: repaint failed
repaint after 1049h (3)          -> RuntimeError: repaint failed
--- после смены экрана
    pane 0: ['ALTZERO']         <- терминал отработал: альт-экран нарисован
    pane 1: ['PLAIN1']
    renderer retained: ['PLAIN1']  <- рендерер стоит на кадре ДО смены
```

Модель обновилась, удержанные клетки рендерера — нет. Это и есть
«приложение не запускается» в терминах пользователя.

## Чувствительность: тесты падают по делу и не переспецифицированы

Проверено на **копии репозитория** (`git worktree` в scratchpad'е;
рабочее дерево не трогалось), четырьмя сборками.

| Сборка `st_test` | Результат |
|---|---|
| `HEAD` без изменений | **7 красных**, 2 зелёных |
| Гипотеза `T3`: `Vterm::exposeAll()` + вызов у **всех** видимых панелей на отказ `renderer->update()` | **9 из 9 зелёных**, без единой правки в тестах |
| Мутация: тот же вызов, но `expose()` вместо `exposeAll()` (ловушка Р2 плана — `expose()` только взводит `outputPending`, строк не повреждает) | **7 красных** — ловится |
| Мутация: `exposeAll()` только у **сфокусированной** панели (та же ошибка, что уже живёт в `application.cpp:663-667`) | **2 красных** (`...focused_pane...`, `...third_pane...`) — ловится |

Вторая строка — главная: **гипотеза исправления делает набор зелёным
целиком, не требуя менять ни одного ожидания.** Значит тесты держатся за
поведение («кадр сел»), а не за детали текущей реализации, и задача `T3`
не упрётся в мои же утверждения.

Третья и четвёртая строки — точечная проверка чувствительности из фазы 6
скилла, в самой полезной её форме: не «сломаем случайную строчку», а «а
если исправление напишут почти правильно». Обе почти-правильные версии
краснеют.

Патч гипотезы (для `T3`, как ориентир, не как предписание):

```cpp
// lib/shitty/vterm.h, рядом с expose()
virtual void exposeAll() = 0;

// lib/shitty/vterm.cpp
void VtermImpl::exposeAll() {
    exposeFrames();   // frame_pri->expose(); frame_alt->expose();
    redraw();
}

// lib/shitty/application.cpp, presentTerminal()
if (!composer.renderer->update(frameUpdates.data(), frameUpdates.length())) {
    for (size_t at = 0; at < framePanes.length(); ++at) {
        framePanes[at].terminal->exposeAll();
    }
    composer.window->requestFrame();
    return false;
}
```

Копия репозитория и все четыре сборки удалены; рабочее дерево содержит
только `tst/test_pane_alt_screen.py` и этот отчёт.

## Пункт 3 задачи: смена `shapes` помимо 1049 — достижима, но только внутри альт-экрана

Требование было: покрыть **механизм, а не последовательность 1049**.
Разобрано по коду, вот что нашлось.

`update.shapes` — это указатель `cf` (`vterm.cpp:2743`, `:2759`), то есть
идентичность объекта `Screen`. Он меняется ровно тремя способами:

1. **`cf` переключается между `frame_pri` и `frame_alt`** —
   `vterm.cpp:3704`, `:3739`, `:3756`;
2. **`Screen` пересоздаётся** — `createAlternateScreen()` /
   `createInactiveAlternateScreen()` (`:3695`, `:3708`, `:3722`, `:3759`);
3. **`resizeScreen()`** (`vterm.cpp:2285`) — она аллоцирует **новый**
   `Screen` и подменяет указатель.

Путь 3 — единственный, не связанный с альт-экраном, и он **протоколом
недостижим в нужной форме**, причём не случайно:

- изменить сетку одной панели можно только через раскладку —
  `applyLayout()` → `paneResized()` (`session.cpp:857`);
- `paneResized()` (`vterm.cpp:8818`) **безусловно** делает `cf->expose()`
  у каждой панели вкладки, а не только у той, чья сетка поехала. Это
  сделано намеренно коммитом `98a08f42` и прокомментировано на месте
  ровно этим дефектом: «панель, чья сетка не изменилась, отдала бы
  ничего, кадр был бы отвергнут, и окно просило бы его заново вечно»;
- значит после любого ресайза (окна, шрифта, сплита) **молчащих панелей
  не остаётся** — а без молчащей соседки дефект не воспроизводится по
  построению;
- `DECCOLM` (`\033[?3h`) сетку панели тоже не двигает: `switchColMode()`
  (`vterm.cpp:3664`) уходит в `windowOperation(8, ...)`, то есть просит
  **окно**, а не панель;
- `RIS` (`\033c`) на панели, которая ни разу не была на альт-экране,
  `cf` не трогает вовсе.

**Вывод: смена `shapes` у одной панели из двух при молчащей соседке
достижима только через семейство альтернативного экрана.** Это находка,
а не пропуск: половина механизма (путь 3) уже закрыта в продукте и
закрыта правильно, поэтому «тест на любую другую смену формы» упирается
не в бедность протокола, а в то, что другой формы не осталось.

Чтобы тест всё-таки проверял **механизм**, внутри этого семейства взяты
три разные дороги, из которых две ничего не знают о 1049:

- **`\033[?47h`** — другая последовательность и другая ветка парсера
  (`parser.cpp:1245` → `setAlternateScreen(true, false)`), без очистки и
  без сохранения курсора, которые делает 1049;
- **`\033[?1049h`, посланный панели уже на альт-экране** — самый чистый
  случай: `switchScreenBufferMode()` пересобирает `frame_alt` на месте
  (`vterm.cpp:3695`), **ни один экран не входится и не покидается**,
  меняется только указатель — и окно всё равно встаёт. Ничего из
  «основной против альтернативного» этот случай объяснить не может;
- **`\033[?1049l`** — обратное направление.

Исправление, написанное против последовательности 1049, а не против
указателя, оставит красными первый и второй тесты.

## Допущения (делегированный режим — принято мной)

1. **Оракул — `REPAINT`, а не новая команда протокола.** План допускал
   «счётчик кадров»; счётчика в протоколе нет, а `REPAINT` уже отдаёт
   ровно тот `bool`, который вернул `presentTerminal()`. Новых команд не
   заводил — `test_mode.cpp` чужая территория (граница задачи).
2. **Устаканивание — три кадра после сплита**, не ожидание. Число, а не
   интервал: геометрия окна устаканивается за первые кадры, а севший
   кадр сам вызывает `consume()` у всех говоривших панелей, поэтому
   молчание соседки не «наступает со временем», а достигается кадром.
   Обосновано в докстринге `split_in_two()`.
3. **Критерий `T1` про различие ширин не использован.** Он неверен (при
   40 колонках обе панели по 18), о чём предупреждала и постановка, и
   отчёт `T1`. Ни один тест здесь на ширины не опирается вовсе.
4. **Зеркальный случай `1049l` построен через «альт-экран до сплита».**
   Панель уходит на альт-экран, пока она в вкладке одна (этот кадр
   садится — соседки нет), затем сплит, затем `1049l`. Это короче и
   честнее, чем «дефект → расклинить соседкой → выйти», и не содержит
   утверждений о промежуточных отвергнутых кадрах.
5. **Ни один тест не утверждает, что кадр был отвергнут.**
   Утверждается только «кадр обязан сесть». Тест вида «убедимся, что
   сейчас отказ» покраснел бы на исправлении, то есть работал бы против
   `T3`; в контроле
   `test_a_frame_no_pane_is_silent_in_settles` это оговорено
   комментарием явно.
6. **Два зелёных контроля оставлены в наборе.** Формально задача просила
   красные; контроли добавлены потому, что без них красные не отличают
   «исправлено» от «сломано в другую сторону».

## Прогоны

| Что | Команда | Итог |
|---|---|---|
| Сборка | `./build st st_test pt_test unit_tests pty_test_helper toml_dump -j 10` (штатный Apple clang, `CC`/`CXX` не выставлялись) | зелёная, exit 0 |
| Юнит-тесты | `SHITTY_PTY_TEST_HELPER=… TMPDIR=<вне чекаута> .build/unit_tests` | **`OK: 949`**, эталон |
| Новый модуль | `python3 -m unittest test_pane_alt_screen -v` | `Ran 9`, `FAILED (failures=7)` — цель задачи |
| Он же ×3 и в случайном порядке | — | тот же результат каждый раз |
| Весь набор `tst/` | `python3 -m unittest discover -p 'test_*.py' -v` | `Ran 6380`, `failures=15, errors=90` — из них **7 мои и ожидаемые**, остальные 98 в других модулях |

## Было красным до начала работы

Полный набор `tst/` даёт **105 отказов**: `failures=15, errors=90`.
Семь из них — мои, намеренные. Остальные 98 — в 35 других модулях
(`test_ghostty_pagelist_*`, `test_bitmap_font_render`, `test_protocols`,
`test_parser*`, `test_font_resolver`, `test_iterm2_*`, `test_contour_*`
и прочие). Мой файл на них влиять не может: каждый тест поднимает
собственный процесс `st_test`, общего состояния нет.

Два прогона — под нагрузкой (параллельно шла сборка) и на свободной
машине — дали **посимвольно одинаковые** `failures=15, errors=90` и
одинаковый разрез по модулям. То есть это не флап от нагрузки.

**Оговорка к числу из постановки.** Мне называли ориентир «~22 фоновых
отказа»; фактически их 98. Причина видна из разреза сообщений:

| Сообщение | Сколько |
|---|---|
| `RuntimeError: shitty gave no reply within 10s` | 70 |
| `RuntimeError: invalid hex input` | 69 |
| `RuntimeError: invalid font load response` | 13 |

`REPLY_TIMEOUT = 10` — это предел на ответ, заведённый `F1c` (`fc46a33a`,
«зациклившийся терминал роняет набор, а не вешает его»). Проверил, не
слишком ли он тесен: прогнал `test_protocols` с пределом **120 секунд** —
модуль не завершился и за **10 минут**. Значит терминал в этих тестах не
медленный, а **висит**, и `F1c` не создал эти отказы, а сделал их
видимыми: раньше на их месте была подвисшая джоба. Разбираться, почему он
висит (по сообщениям — что-то вокруг загрузки шрифтов), — не задача `T2`;
фиксирую как находку для `R3-qa`, потому что число «~22» в планах волны
устарело и на него нельзя опираться при приёмке.

`python3 dev/style.py` не запускался намеренно: установленный
clang-format 23.1.0 против ожидаемого 21 переформатирует ~130 чужих
файлов (предупреждение постановки).

## Что осталось незакрытым

- **Паритет Metal ↔ reference** (§7.4 диагноза) не покрыт. `st_test`
  ходит через эталонный рендерер; продуктовый путь macOS —
  `render_metal.mm:1113`, дословная копия проверки, — этими тестами не
  наблюдается. Утверждение «дефект в обоих» держится на чтении кода и на
  замерах диагноза, не на тесте. Место для этого — `test_gpu_parity.py`,
  который сегодня однопанельный; в границы `T2` не входит.
- **Пересоздание рендерера при двух панелях** (§7.5, второй экземпляр
  той же ошибки, `application.cpp:663-667`) не покрыт: протоколом нет
  способа уронить поверхность. Мутационная проверка выше показывает
  только, что тесты **отличили бы** починку одной панели от починки всех.
- **Диагностика подряд идущих отказов** (`Р3` плана) не покрыта: её
  ещё нет, и порог задаёт `T3`. Тест на stderr — за `R3-test`.
- **Настоящий потомок и `winsize` после сплита** (§7.6) — территория
  `T5`, уже закрыта юнит-тестом
  `Pty::EveryPanesChildIsBornWithThatPanesSize`.
- **Mutation-тестирование инструментом** не проводилось: в проекте
  ничего такого не настроено, ставить ради одного прогона не стал.
  Вместо него — четыре ручные сборки в §«Чувствительность».
