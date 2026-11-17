# PORT-275 (fork issue 642): анализ 7 кнопок NOT-ON-BAR в Ennorath

Дата: 2026-11-17. Матрица кнопок от 2026-11-17 (main c73bb057). Все семь кнопок — by design: RotWK
показывает их ровно так же, как порт; различий в правилах нет, движок не тронут.

## Сводная таблица

| кнопка | объект | причина | RW-адрес | INI |
|---|---|---|---|---|
| `Command_PurchaseUpgradeRohanForgedBladesForPeasants` | RohanPeasantHorde | `Upgrade_RohanForgedBladesForPeasants` не объявлен в Ennorath -> null `UpgradeTemplate` -> Hidden | RW 0x943050 | commandbutton.ini (`ForPeasants`), commandset.ini:2873 |
| `Command_PurchaseUpgradeRohanHeavyArmorForPeasants` | RohanPeasantHorde | `Upgrade_RohanHeavyArmorForPeasants` не объявлен -> null -> Hidden | RW 0x943050 | commandbutton.ini (`ForPeasants`), commandset.ini:2873 |
| `Command_SpecialAbilityFortressHaradSpyTechAI` | HaradFortressCitadel | слот 32 (индекс 31) вне окон push 8-15 / 16-24 / 25-30 при `InitialVisible = 8` | RW 0x97D718, RW 0x71CF0A | commandset.inc:25069, commandbutton.inc:46220 |
| `Command_GaladrielMirrorDissentLocked` | GaladrielDarkMirror | слот 4 (индекс 3) базового набора вне окна (0, 4) после `resetRanges` | RW 0x97D718 | commandset.inc:27532, commandbutton.inc:54418 |
| `Command_GaladrielMirrorWardByMirror` (база) | GaladrielDarkMirror | слот 3 (индекс 2) того же набора — то же правило | RW 0x97D718 | commandset.inc:27532, commandbutton.inc:54385 |
| `Command_GaladrielMirrorDissent` | GaladrielDarkMirror8 | тот же механизм: набора `Set8` нет без `Upgrade_Level_8` | RW 0x8B7C68 | commandset.inc:27540, commandbutton.inc:54400 |
| `Command_GaladrielMirrorWardByMirror` (8) | GaladrielDarkMirror8 | тот же механизм | RW 0x8B7C68 | commandset.inc:27540, commandbutton.inc:54385 |

## Rohan: ForgedBladesForPeasants / HeavyArmorForPeasants

`RohanPeasantHorde` (neutralunits.ini:5255) использует `RohanPeasantCommandSet` (commandset.ini:2873);
слоты 3 и 4 — две кнопки `ForPeasants`. Правило RW (getCommandAvailability, arm OBJECT_UPGRADE):

- RW 0x943050: кнопка `OBJECT_UPGRADE` резолвит `UpgradeTemplate` по имени; null -> RW 0x942C5D (Hidden).
- DONOR BFME2 0x00339679 (`INI_parseUpgradeTemplate.cpp`): неизвестный апгрейд хранится как null.
- Ennorath не объявляет `Upgrade_RohanForgedBladesForPeasants` / `Upgrade_RohanHeavyArmorForPeasants`
  (upgrade.ini:2105-2175 — только tech/personal апгрейды, `SubUpgradeTemplateNames` закомментированы).

Вывод: RotWK прячет обе кнопки точно так же. Для матрицы владельца — строки в `matrix_bydesign.tsv`.

## Harad: SpyTechAI

`HaradFortressCitadel` (haradfortress.ini:799) использует `HaradFortressCommandSet` (commandset.inc:25069):
`InitialVisible = 8`, кнопки push окон 8-15, 16-24, 25-30 (0-based, «Starts its counting at 0»).
`SpyTechAI` — слот 32, индекс 31: вне всех окон. Правило RW:

- RW 0x97D718 (`resetRanges`): пустой стек диапазонов получает `(0, InitialVisible)`.
- RW 0x71CF0A / RW 0x75CA97: пустой стек -> `(0, 33)`; иначе вершина стека.
- RW 0x943D6F (`populateCommand`): окно i показывает слот `rangeStart + i`, только `i < rangeCount`.

Вывод: кнопка недостижима в RotWK тоже (AI-only в наборе, который человек не получает).

## Galadriel: DissentLocked / WardByMirror

Наборы (commandset.inc:27532 / 27540 / 27548): базовый — 1 Passive, 2 Vision, 3 WardByMirror,
4 DissentLocked; `Set8` — 4 Dissent; `Used` — 1 Passive. Кнопки commandbutton.inc:54385 / 54400 /
54418; `DissentLocked` несёт `NEED_UPGRADE NeededUpgrade = Upgrade_Level_8` (includes/upgrade.inc:96).

Правило RW: после `resetRanges` (RW 0x97D718) стек диапазонов = `(0, InitialVisible)`; слоты 3 и 4
(индексы 2 и 3) базового набора — вне окна, если push-кнопки нет. В базовом наборе её нет, поэтому
`DissentLocked` и `WardByMirror` не на баре — как в RotWK.

Механизм замка: способность создаёт объект, который вешает `GaladrielMirrorUsedModifier`
(includes/attributemodifier.inc:15996; BonusName galadrielmirror.ini:227 / 277 / 337) — модификатор
даёт `Upgrade_GaladrielMirrorUsed` (upgrade.ini:3445), а `CommandSetUpgrade::upgradeImplementation`
(RW 0x8B7C68) меняет набор на `GaladrielMirrorUsedCommandSet` через `Object::getCommandSetName`
(m_commandSetOverride, Object.cpp:264). Порт повторяет это (UpgradeModuleClasses.cpp:489); тест
`PORT-275*` фиксирует, что бар перевычисляется в том же update.

## Что не потребовалось

Движок не менялся: все три правила порта уже совпадают с RW (проверено дизассемблером). В PR —
только тесты `engine/tests/test_port_275.cpp`, лейн `engine/cmake/Port275.cmake`, эта записка и
строка ночного дозора.
