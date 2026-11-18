# PORT-275 (fork issue 642): анализ 7 кнопок NOT-ON-BAR в Ennorath

Дата: 2026-11-17, дополнено 2026-11-18. Матрица кнопок от 2026-11-17 (main c73bb057). Вывод по движку:
все семь кнопок удерживают уже перенесённые правила RotWK — расхождения порта нет, движок не тронут.
По Galadriel остаётся одно чтение INI (какой именно из трёх перенесённых гейтов скрывает кнопки) —
песочница потеряла распакованный INI мода; кандидаты и их строки названы ниже, чтобы закрыть одним grep.

## Сводная таблица

| кнопка | объект | причина | RW-адрес | INI |
|---|---|---|---|---|
| `Command_PurchaseUpgradeRohanForgedBladesForPeasants` | RohanPeasantHorde | `Upgrade_RohanForgedBladesForPeasants` не объявлен в Ennorath -> null `UpgradeTemplate` -> Hidden | RW 0x943050 | commandbutton.ini (`ForPeasants`), RohanPeasantHordeCommandSet слот 3 (commandset.ini:2873) |
| `Command_PurchaseUpgradeRohanHeavyArmorForPeasants` | RohanPeasantHorde | `Upgrade_RohanHeavyArmorForPeasants` не объявлен -> null -> Hidden | RW 0x943050 | commandbutton.ini (`ForPeasants`), слот 4 (commandset.ini:2873) |
| `Command_SpecialAbilityFortressHaradSpyTechAI` | HaradFortressCitadel | слот 32 (индекс 31) вне окон push 8-15 / 16-24 / 25-30 при `InitialVisible = 8` | RW 0x97D718, RW 0x71CF0A | commandset.inc:25069, commandbutton.inc:46220 |
| `Command_GaladrielMirrorDissentLocked` | GaladrielDarkMirror | один из трёх перенесённых гейтов (см. ниже) — в RotWK то же | RW 0x97D718 / RW 0x943343 / RW 0x92FF5C | commandset.inc:27532, commandbutton.inc:54418 |
| `Command_GaladrielMirrorWardByMirror` (база) | GaladrielDarkMirror | то же | те же | commandset.inc:27532, commandbutton.inc:54385 |
| `Command_GaladrielMirrorDissent` | GaladrielDarkMirror8 | то же (набор `Set8`) | те же | commandset.inc:27540, commandbutton.inc:54400 |
| `Command_GaladrielMirrorWardByMirror` (8) | GaladrielDarkMirror8 | то же | те же | commandset.inc:27540, commandbutton.inc:54385 |

## Rohan: ForgedBladesForPeasants / HeavyArmorForPeasants

`RohanPeasantHorde` использует `RohanPeasantHordeCommandSet`; слоты 3 и 4 — две кнопки `ForPeasants`
(MATRIX.tsv:944-945 уже помечает оба апгрейда как несуществующие). Правило RW (getCommandAvailability,
arm OBJECT_UPGRADE):

- RW 0x943050: кнопка `OBJECT_UPGRADE` резолвит `UpgradeTemplate` по имени; null -> RW 0x942C5D (Hidden).
- DONOR BFME2 0x00339679 (`INI_parseUpgradeTemplate.cpp`): неизвестный апгрейд хранится как null.
- Ennorath не объявляет `Upgrade_RohanForgedBladesForPeasants` / `Upgrade_RohanHeavyArmorForPeasants`
  (upgrade.ini:2105-2175 — tech/personal апгрейды, `SubUpgradeTemplateNames` закомментированы).

Вывод: RotWK прячет обе кнопки точно так же. Для `matrix_bydesign.tsv`: INI commandbutton.ini
(`Command_PurchaseUpgradeRohan*ForPeasants`), RW 0x943050.

## Harad: SpyTechAI

`HaradFortressCitadel` использует `HaradFortressCommandSet` (commandset.inc:25069): `InitialVisible = 8`,
push-окна 8-15, 16-24, 25-30 (0-based, «Starts its counting at 0»). `SpyTechAI` — слот 32, индекс 31:
вне всех окон. Правило RW:

- RW 0x97D718 (`resetRanges`): пустой стек диапазонов получает `(0, InitialVisible)`.
- RW 0x71CF0A / RW 0x75CA97: пустой стек -> `(0, 33)`; иначе вершина стека.
- RW 0x943D6F (`populateCommand`): окно i показывает слот `rangeStart + i`, только `i < rangeCount`.

Кнопка недостижима и в RotWK; имя и модули заканчиваются на `AI` (ModuleTag_HaradSpyTrainingStarterAI,
MATRIX_GAPS.tsv) — кнопка компьютерного игрока, человек её никогда не получает. Для `matrix_bydesign.tsv`:
INI commandset.inc:25069 (`HaradFortressCommandSet`), RW 0x97D718 / RW 0x943D6F.

## Galadriel: DissentLocked / WardByMirror

Наборы (commandset.inc:27532 / 27540 / 27548): базовый — 1 Passive, 2 Vision, 3 WardByMirror,
4 DissentLocked; `Set8` — 4 Dissent; `Used` — 1 Passive. Кнопки commandbutton.inc:54359 (Passive,
NONPRESSABLE), 54371 (Vision), 54385 (WardByMirror, NEED_TARGET_POS), 54400 (Dissent,
+CONTEXTMODE_COMMAND), 54418 (DissentLocked, NEED_UPGRADE `Upgrade_Level_8`, includes/upgrade.inc:96).
Модули сил стоят на объектах (MATRIX_GAPS.tsv:79-81: `ModuleTag_MirrorWardPower` / `ModuleTag_MirrorDissentPower`,
OCLSpecialPower). Механизм замка доказан: способность создаёт объект, который вешает
`GaladrielMirrorUsedModifier` (includes/attributemodifier.inc:15996; BonusName galadrielmirror.ini:227/277/337)
-> `Upgrade_GaladrielMirrorUsed` (upgrade.ini:3445) -> `CommandSetUpgrade::upgradeImplementation`
(RW 0x8B7C68) -> `Object::getCommandSetName` отвечает `m_commandSetOverride` (порт: UpgradeModuleClasses.cpp:489,
Object.cpp:264; тест `PORT-275*` фиксирует, что бар показывает сменённый набор уже в следующем update).

**Что скрывает кнопки слотов 3/4 — три кандидата, все перенесены и совпадают с RW** (песочница потеряла
распакованный INI; решающее чтение — один grep по `commandset.inc` / `commandbutton.inc` / `specialpower.ini`):

1. `InitialVisible = 2` в наборах зеркала: базовое окно (0, 2) (RW 0x97D718) не покрывает слоты 3/4,
   push-кнопки в наборах нет -> недостижимы. INI: строка `InitialVisible` в commandset.inc:27532-27550.
2. У `Command_GaladrielMirrorWardByMirror` / `...Dissent(Locked)` нет ни `InPalantir`, ни `Radial`:
   кнопка вне обоих баров (RW 0x92FF5C: дуга только InPalantir +0x102, окна 0-5; RW 0x92F082: боковой
   только Radial +0x101). INI: флаги в commandbutton.inc:54385-54433 (у Vision — Radial, она на баре).
3. `RequiredSciences` у `SpecialAbilityGaladrielMirrorWardByMirror` / `...Dissent`: ни одной науки у игрока
   нет -> Hidden в arm SPECIAL_POWER (RW 0x943343 -> RW 0x94337F..0x9433D1). INI: specialpower.ini
   (блоки этих сил); свип науки не покупает, поэтому в матрице они скрыты, а в игре после покупки науки
   появятся.

Любой из трёх — by design: порт применяет то же правило (доказано дизассемблером и тестами PORT-250 /
PORT-275). Строка в `matrix_bydesign.tsv` закрывается одним чтением: если (1) — `commandset.inc:27532`
(InitialVisible), RW 0x97D718; если (2) — `commandbutton.inc:54385` (флаги), RW 0x92F082; если (3) —
`specialpower.ini` (RequiredSciences), RW 0x943343.

## Что не потребовалось

Движок не менялся: все правила-кандидаты порта уже совпадают с RW. В PR — только тесты
`engine/tests/test_port_275.cpp`, лейн `engine/cmake/Port275.cmake`, эта записка и строка ночного дозора.
