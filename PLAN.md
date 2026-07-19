# Оставшийся протокольный план Zutty

Уже закрытые пункты из первоначального аудита удалены. Графические протоколы
оставлены отдельным отложенным блоком: текущая задача их не включает.

## Protocol core и безопасность

- Довести parser до общего ECMA-48 dispatcher с отдельными parameter,
  intermediate и final bytes. Текущий parser уже безопасно игнорирует
  неизвестные и переполненные последовательности, но всё ещё состоит из
  специализированных состояний.
- Добавить fuzz target для parser. ASan/UBSan прогнать позднее в окружении не
  на musl.
- Убрать синхронную запись больших clipboard response и paste в PTY: partial
  write/EAGAIN должны обслуживаться event loop без заморозки UI.

## Совместимость с `xterm-256color`

Проверить всю локальную terminfo, а оставшиеся рекламируемые возможности либо
реализовать, либо перестать объявлять:

- реальная blink-анимация текста и blinking cursor;
- visual bell через `DECSCNM`;
- printer controls;
- meta mode `?1034`;
- корректное применение dynamic selection colors `OSC 17/19` рендерером;
- динамическая палитра для уже находящихся на экране indexed-color cells, а не
  только для текста, появившегося после `OSC 4`.

## Unicode и grapheme clusters

- Combining characters.
- Grapheme clusters.
- Variation selectors.
- ZWJ sequences.
- Shaping и корректная ширина всего cluster, включая selection/copy,
  editing, scrollback и renderer.

## OSC и shell integration

- OSC 133 semantic prompt/shell integration.
- OSC 9/99 notifications и progress reporting.
- Title stack/query (`CSI 20/21/22/23 t`).

## Keyboard

- Убрать приближённую US-layout таблицу из alternate/base-layout keys и брать
  реальные layout/text данные frontend-а.
- Реализовать все XTMODKEYS resources, а также их query/reset semantics;
  сейчас реально работает только `modifyOtherKeys` (resource 4).

## Mouse

- Mode 1001 highlight tracking.
- Mouse leave reporting.

## Window protocols

- In-band resize notifications.
- Дополнить XTWINOPS теми операциями, которым нужны title stack, сведения о
  monitor/screen и fullscreen state.

## DEC/xterm completeness

- DECSCA и selective erase DECSED/DECSEL.
- Rectangular operations: DECFRA, DECCRA, DECERA, DECCARA, DECRARA, checksum.
- Double-width/double-height line attributes.
- User-defined keys.
- Locator protocol.
- Printer controls.
- LED controls.
- Full NRCS family и DECNRCM.
- Reverse wraparound.
- Tektronix emulation.

## Тестирование

- Полная default-parameter matrix для CSI/DEC operations.
- Differential traces против xterm, foot и kitty.
- Актуальный автоматический `vttest`.
- Parser fuzzing и sanitizer-прогоны в подходящем окружении.
- Renderer/integration tests для blink, dynamic palette, selection colors и
  grapheme shaping.

## Отложено: графика

- Sixel.
- Kitty graphics.
- iTerm2 inline/multipart images.
- ReGIS.

Kitty graphics/APC уже безопасно игнорируется и не печатает payload как текст;
полная реализация chunking, transports, placements и quotas отложена.

## Порядок оставшихся работ

1. Grapheme storage/shaping и renderer tests.
2. Blink, dynamic indexed colors и selection colors.
3. Async PTY output для OSC 52/paste.
4. Title stack, OSC 133/9/99 и in-band resize.
5. XTMODKEYS, mouse 1001/leave и оставшиеся XTWINOPS.
6. DEC selective/rectangular/line/NRCS operations.
7. Полный аудит `xterm-256color`, differential traces, `vttest` и fuzzing.
