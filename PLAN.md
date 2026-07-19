# Отложенные работы Zutty

Активная не-графическая часть протокольного плана закрыта. Ниже оставлены
только работы, исключённые из текущего прохода.

## Sanitizers

- Прогнать parser fuzz target под ASan/UBSan в окружении не на musl.

## Графические протоколы

- Tektronix emulation.
- Sixel.
- Kitty graphics.
- iTerm2 inline/multipart images.
- ReGIS.

Kitty graphics/APC уже безопасно игнорируется и не печатает payload как текст;
полная реализация chunking, transports, placements и quotas отложена.
