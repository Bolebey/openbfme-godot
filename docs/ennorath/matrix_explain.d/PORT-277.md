# PORT-277 / issue 644: восемь range-finder кнопок (draft / STOP)

Проверяется граница **после выстрела**. Это объяснение исходных красных
строк, не новый прогон MATRIX.tsv и не присвоение кнопкам PASS. Сами
сгенерированные карты не менялись. Внешний INI-корпус Ennorath 1.9.8.3
загружается RetailObjectWorld штатными парсерами; определения игры в Git
не включены. Имена тестов: `PORT-277 real definitions: ...`.

## Вердикты данных и RW

- `IsenfurtWchter Command_IsenfurtWchterSmite`: **как в RotWK** — нет
  HordeContain; HordeAttackNugget ничего не делает. RW FUNCTION START
  `0x911A58` (object) / `0x911AC9` (position): проверки contain и его
  horde interface предшествуют lock/dispatch. Урон не берётся из другого
  слота или из range-finder.
- `IsenfurtWchterCreep Command_IsenfurtWchterSmite`: **как в RotWK** —
  та же причина и те же RW функции, отдельный тест реального объекта.
- `IsenfurtWchterHordeHerold Command_IsenfurtWchterSmite`: **NO-SEND до
  нагета**. Кнопка ссылается на SpecialAbilityZealotBoomAxe, starter и
  updater — на SpecialAbilityWestfoldWchterSpear, определения которого
  нет в проверенном корпусе. RW FUNCTION START `0x73B22F` разрешает имя;
  `0x68C26D` ищет интерфейс именно шаблона, `0x8969D1` сравнивает указатели
  шаблонов. В FUNCTION START `0x82D925` сначала проверяется source-type
  mask (`0x82D94E..0x82D961`, вызов `0x68DF46`), а после поиска интерфейса
  (`0x82D96A`) его отсутствие отвергается в `0x82DA81..0x82DA86` даже
  без проверки готовности. Это данные/общий путь
  кнопки, не ошибка HordeAttackNugget. Принудительный выстрел предусмотрен тестом
  независимо, но до него прогон не дошёл; исправлять INI или добавлять обход в движок нельзя.
- `GondorRangerHorde Command_MenLongShotAbility`: **dummy-оружие без собственных
  нагетов; дальнейший выбор слота не установлен**. MenLongShotFakeWeapon в QUINARY не содержит нагетов.
  RW FUNCTION START `0x875550` выдаёт членам орды приказы в точку,
  сам не добавляя урон; дальнейший общий выбор слота ещё не проверен.
  Отдельная атака определена
  отдельно: OCLSpecialPower / OCL_LongShotAbilityGondor →
  MenLongShotObjectGondor → FireWeaponUpdate. RW FUNCTION START `0x8C75D8`
  обрабатывает OCL в точке, FUNCTION START `0x88F554` обходит оружия
  FireWeaponUpdate с задержками и собственными координатами. Это не
  повод добавлять fallback-урон в HordeAttackNugget.
- `DwarvenDain_mod Command_SpecialAbilityDrarSpeerfeuer`: **range-finder
  как в RotWK, урон отдельно**. У героя нет HordeContain, поэтому
  MenLongShotWeapon — no-op (`0x911AC9`). OCL_SpeerfeuerAbility создаёт
  DrarWarnShotObject и DrarSpeerfeuerEgg, далее отдельный
  OCL_DrarSpeerfeuerProjectile → DrarSpeerfeuerProjectile →
  FireWeaponUpdate / SpeerfeuerProjectileWeapon. Для OCL/FireWeaponUpdate
  те же FUNCTION START `0x8C75D8`, `0x88F554`.

## Орды с настоящим метательным оружием

- `IsenfurtWchterHorde Command_IsenfurtWchterSmite`: проверяемый граф —
  IsenfurtWchterSmiteRangeFinder → TERTIARY → IsenfurtWchterSpearThrown
  → projectile/warhead. RW FUNCTION START `0x911A58` / `0x875221`.
- `DwarvenAxtschwingerHorde Command_AxtschwingerAxeThrow`: граф —
  DwarvenZealotBoomAxeRangeFinder → TERTIARY →
  DwarvenZealotBoomAxeThrownEdain → projectile/warhead. Те же RW функции.
- `AngmarDarkRangerHorde Command_AngmarRangersIceArrowAbility`: граф —
  HaradrimBarbedArrowModRangeFinder → QUINARY → AngRangerIceShotArrowMod
  → projectile/warhead. Те же RW функции. Unlock starter зависит от
  Upgrade_ObjectLevel1; проверка после выстрела не подменяет эту проверку
  доступности кнопки.

## Почему сообщение матрицы не доказывает поломку нагета

`SweepEffectProbe::take` обходит и орду, и её членов, включая их
WeaponFireSpecialAbilityUpdate. `inspectWeaponSpecial` добавляет DAMAGE
без оружия, если holder без HordeContain или слот dummy; `compare` для
такого ожидания всегда оставляет результат пустым. Поэтому «caster has
no HordeContain» в строке **орды** может относиться к модулю **бойца**,
а не к источнику реально выстрелившего range-finder. Для Long Shot и
Drar отдельный OCLSpecialPower не превращает невозможное ожидание урона
от range-finder в возможное. Общий probe здесь не меняется; результаты
trace реальных выстрелов пока не получены; применение этого объяснения
к каждой строке остаётся INFERENCE до запуска.

## Runtime — все восемь заблокированы входными данными

Полный бинарник собран (Release, Godot OFF, Ninja `-j1`, основа 3d017a41).
`PORT-277*`: 11 тестов, 2 passed / 9 failed, 30652 проверки / 21 ошибка.
Каждый из восьми реальных тестов остановился на отсутствующем
`Data/Scripts/Scripts.ini` в отчёте RetailObjectWorld, до проверок выстрела
и до диагностики pre-shot. Никакой пустой замены и фильтрации ошибки нет.
Это [S-7473](../../stops.d/S-7473.md), не доказанный стоп Local 76.
Имена восьми тестов и подробный результат — в [отчёте](../PORT-277.md).

Исходные два синтетических теста проходят (отдельно 2 / 84, SUCCESS).
Новый dummy-тест **падает**: PRIMARY вместо SECONDARY в обоих subcase,
object-subcase наносит урон. Это не доказывает соответствие или отличие
от RW: общее правило выбора оружия ещё надо прочитать, ожидание теста
не подтверждено этим замером ([S-7474](../../stops.d/S-7474.md)).
Поэтому нельзя пока заключать, что Long Shot не использует другой слот.

Все описания real-data графов выше — проверка определений и чтение RW,
**не успешный runtime и не восемь PASS**. Причина Herald статически
локализована в binding/gate ([S-7472](../../stops.d/S-7472.md)), но его
HUD-клик тоже не воспроизведён. После rebase новая основа не собиралась;
этот PR остаётся draft до восстановления входных данных и полного
повторного прогона. Матрица не перезаписана.
