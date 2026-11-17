# PORT-277 / issue 644 — предварительная проверка существующего пути

База: `a0302bff7d6a157af8102c50be8e09dfcecfae9e`.
Движок пока не меняется: issue требует сначала проверить, не работает ли уже
передача атаки бойцам в main. Синтетический тест и полный `openbfme_tests`
готовятся; вывод о воспроизведении до прогона не сделан.

## Прочитанный RotWK 2.01

Адреса прочитаны `tools/mod/rw.py dis` по проверенному game.dat 2.01.

- `RW 0x91197E`: парсер добавляет базовую таблицу и таблицу nugget
  `0xC7BC7C`. `RW 0x911B39`: конструктор устанавливает vtable `0xC7BCAC`,
  `ClosestMemberOnly` (+0x148) = false, `LockWeaponSlot` (+0x14C) = 5.
- `RW 0x9119CB`: применимость. Сначала базовый фильтр `0x90D77C`.
  Затем источник по ID, contain, horde interface. Для COMBO_HORDE выбирается
  боец с наибольшей дальностью (slot 0x50), иначе первый не-BANNER
  (slot 0x114, аргумент 0xA4). Если найден боец с текущим оружием,
  вызывается применимость этого оружия `0x6CDBF3`. Отсутствие источника,
  contain, интерфейса, бойца или его оружия возвращает true, а не урон.
- `RW 0x911A58`: доставка в объект. Нет источника / contain / horde interface
  — возврат без действия. Иначе при LockWeaponSlot != 5 вызывается
  `0x69121A(slot, LOCKED_TEMPORARILY)`, затем horde interface slot 4
  `(victim, ClosestMemberOnly)`.
- `RW 0x911AC9`: доставка в позицию. Те же проверки, блокировка и флаг;
  вызывается horde interface slot 0 `(position, ClosestMemberOnly)`.
- `RW 0x69121A`: сначала contain slot 0x168 распространяет блокировку,
  затем источник обновляет SWITCHED_WEAPONS и собственную блокировку.
- `RW 0x875221`: attackTargetNow. Melee-horde gate; возвращение бойцов,
  входящих в гарнизон; копия списка; определение отряда жертвы и
  опциональный ближайший боец. Пропускаются бойцы без записи слота,
  без AI и движущиеся. Ближнее оружие обрабатывается отдельной touch-attack
  веткой. Для дальнего нужны разрешённый ранг и отсутствие уже выполняемой
  атаки на эту цель. У вражеского отряда выбирается боец в дальности
  текущего оружия (при неудаче первый). Проверка `0x6FF7FA` разрешает
  команду `0x66C536(victim, 0x7FFFFFFF, CMD_FROM_AI)`, иначе ставится dirty.

## Сопоставление с портом (чтение кода, ещё не результат теста)

`WeaponDelivery.cpp` уже вызывает `setObjectWeaponLock`, `attackTargetNow`
и `attackPositionNow`. `HordeMemberPass.cpp` уже реализует выдачу приказов.
`ObjectWeapons::createAndFireTempWeaponAt` позволяет отдельно проверить
выстрел range-finder, не затрагивая принадлежащий Local 76 код
`WeaponFireSpecialAbilityUpdate`.

DONOR BFME2 0x0050B7D2 / 0x0050B843 / 0x0050B745
(`Code/GameEngine/Source/GameLogic/Object/Made002CCBCA.cpp`) и
DONOR BFME2 0x00471FFA
(`Code/GameEngine/Source/GameLogic/Object/Contain/HordeContainIface11CSlots.cpp`)
прочитаны как вспомогательные исходники; выводы выше основаны на RW,
не только на доноре.

## Граница проверки

Успешный тест самого nugget не доказывает исправность отправки кнопки,
подготовки способности или данных восьми кнопок. В частности, отсутствие
урона от одиночного источника без HordeContain соответствует RW, а не
доказывает ошибку nugget. Если существующий путь проходит проверку,
следуем прямому указанию issue: остановиться и сообщить владельцу,
не менять чужой WeaponFireSpecialAbilityUpdate и не добавлять второй путь атаки.
