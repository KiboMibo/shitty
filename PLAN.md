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

## Совместимость с `xterm-256color`

Проверить всю локальную terminfo, а оставшиеся рекламируемые возможности либо
реализовать, либо перестать объявлять:

- visual bell через `DECSCNM`;
- printer controls;
- meta mode `?1034`;

## DEC/xterm completeness

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
