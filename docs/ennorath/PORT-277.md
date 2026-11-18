# PORT-277 / fork issue 644 — диагностика, STOP (draft)

## Граница

Исходный HordeAttackNugget уже работает на main. Первоначальное решение
остановиться после воспроизведения заменено ответом владельца: сохранить
синтетические тесты, разобрать восемь реальных кнопок и открыть PR.
Собственные функции WeaponFireSpecialAbilityUpdate (Local 76) не меняются.
В этой ветке **нет изменений engine/src**, нового состояния симуляции,
изменений хэшей, эталонов или сгенерированных карт. Upstream code-fix нет.

## Что прочитано

Проверенные бинарники RW 2.01 / BFME2 1.06 и внешний INI-архив Ennorath
1.9.8.3; контрольные суммы архивов и бинарников проверены до исследования.
Данные игры остаются вне Git. RW прочитан непосредственно через
`tools/mod/rw.py ... dis`, донор Open-BFME-2 использован только как помощь.

| RW FUNCTION START | Наблюдение |
|---|---|
| 0x911A58 / 0x911AC9 | object / position HordeAttackNugget: проверка источника, contain и horde interface; без них возврат. Иначе lock через 0x69121A и dispatch интерфейса. |
| 0x875221 / 0x875550 | Приказы бойцам по объекту / позиции. Самостоятельного добавления DamageNugget здесь нет. Это ещё не доказывает, что последующий общий выбор оружия сохранит dummy-слот. |
| 0x73B22F | Разрешение имени SpecialPower через store. |
| 0x68C26D / 0x8969D1 | Поиск интерфейса способности и точное сравнение указателя шаблона. |
| 0x82D925 | Проверка source-type mask в 0x82D94E..0x82D961; поиск интерфейса в 0x82D96A; отказ при null в 0x82DA81..0x82DA86. |
| 0x8C75D8 | OCLSpecialPower в указанной точке; отдельное создание OCL, не урон range-finder. |
| 0x88F554 | FireWeaponUpdate: оружия с задержками, огонь из координат созданного объекта. |

## Восемь кнопок: определения проверены, runtime заблокирован

Подробности: [matrix_explain.d/PORT-277.md](matrix_explain.d/PORT-277.md).
Это **не восемь пройденных тестов** и не новый прогон матрицы.

| Объект / кнопка | Установлено чтением определений и RW; что ещё не проверено | RW |
|---|---|---|
| IsenfurtWchter / Command_IsenfurtWchterSmite | Без HordeContain; no-op соответствует RW. Реальный runtime-тест не дошёл до выстрела. | 0x911A58 |
| IsenfurtWchterCreep / Command_IsenfurtWchterSmite | То же, отдельное определение и отдельный тест. | 0x911A58 |
| IsenfurtWchterHorde / Command_IsenfurtWchterSmite | TERTIARY, IsenfurtWchterSpearThrown, projectile/warhead. Реальный урон и pre-shot не измерены. | 0x911A58, 0x875221 |
| IsenfurtWchterHordeHerold / Command_IsenfurtWchterSmite | Кнопка и starter/updater называют разные SpecialPower; имя из модулей не определено. Ожидаемый отказ до нагета, S-7472; HUD-клик/runtime не проверены. | 0x73B22F, 0x68C26D, 0x8969D1, 0x82D925 |
| DwarvenAxtschwingerHorde / Command_AxtschwingerAxeThrow | TERTIARY, DwarvenZealotBoomAxeThrownEdain, projectile/warhead. Реальный урон и pre-shot не измерены. | 0x911A58, 0x875221 |
| AngmarDarkRangerHorde / Command_AngmarRangersIceArrowAbility | QUINARY, AngRangerIceShotArrowMod, projectile/warhead; starter разблокируется Upgrade_ObjectLevel1. Реальный урон/pre-shot не измерены. | 0x911A58, 0x875221 |
| GondorRangerHorde / Command_MenLongShotAbility | QUINARY — MenLongShotFakeWeapon без нагетов; отдельный OCL_LongShotAbilityGondor → MenLongShotObjectGondor → FireWeaponUpdate. Нельзя пока утверждать отсутствие переключения dummy → PRIMARY: новая синтетика красная. OCL-урон не измерен. | 0x911AC9, 0x875550, 0x8C75D8, 0x88F554 |
| DwarvenDain_mod / Command_SpecialAbilityDrarSpeerfeuer | Нет HordeContain; range-finder no-op. Отдельный OCL_SpeerfeuerAbility → eggs → OCL_DrarSpeerfeuerProjectile → DrarSpeerfeuerProjectile / FireWeaponUpdate. Урон этого пути не измерен. | 0x911AC9, 0x8C75D8, 0x88F554 |

## Реальная сборка и результаты

Полная цель `openbfme_tests` собрана: Release, Godot OFF, Ninja `-j1`,
1810/1810. Затем выполнена инкрементальная сборка последней версии
`test_port_277.cpp` (7/7). Основа измеренного бинарника — `3d017a41`,
HEAD `f8ebbf32` плюс диагностический diff, сохранённый следующим коммитом.
После последующего rebase на свежий main эти результаты **исторические**:
новая основа не собрана из-за зафиксированного стопа входных данных.

```text
OPENBFME_ENNORATH_INI_DIR=<внешний корпус> engine/build/openbfme_tests -tc="PORT-277*"
[doctest] test cases:    11 |     2 passed |  9 failed | 6887 skipped
[doctest] assertions: 30652 | 30631 passed | 21 failed |
[doctest] Status: FAILURE!
```

- **Исходные два теста проходят**: прямой и projectile/warhead урон
  бойцов через SECONDARY; одиночный источник без HordeContain no-op.
  Отдельный повтор точного фильтра `PORT-277: existing*,PORT-277: single*`:
  2 теста, 84 проверки, 0 ошибок, 6896 исключены фильтром, SUCCESS.
- **Новый dummy-тест красный**: оба subcase получили PRIMARY вместо
  ожидаемого SECONDARY (12 ошибок); object-subcase также уменьшил здоровье
  цели со 100 до 0 (1 ошибка). Это опровергает ожидание нового теста,
  а не доказывает баг движка или поведение RW. Утверждение «не заимствует
  PRIMARY» не принимается без чтения общего выбора/снятия lock в RW.
  Ожидания не ослаблены, красный тест оставлен видимым в draft. S-7474.
- **Каждый из восьми реальных тестов** завершился на проверке отчёта
  RetailObjectWorld: отсутствует `Data/Scripts/Scripts.ini` (8 ошибок).
  Проверены распакованный корпус и список проверенного ZIP: этого файла
  там нет. Нет и полных установок для обычного EnnorathScenario mount.
  До assertion по оружию, приказам, урону, OCL и pre-shot тесты **не дошли**.
  Сообщения диагностики Local 76 не получены; назначать ему причину нельзя.
  Не подставлялся пустой файл и не фильтровалась ошибка загрузки. S-7473.

`test_select.py --base origin/main` на измеренной основе: `5 changed files,
no test map, the #include rule; FULL suite: engine/CMakeLists.txt: no test
can be traced from this file`. FULL, обязательные retail-наборы и игровой
HUD-прогон не запускались; доступны не все исходные игровые файлы. Нет
заявления о прохождении полного набора или готовности к переносу в main.

## Ожидания и оставшаяся работа

Существующие два синтетических теста не ослаблены. Добавлены восемь
проверок внешних определений и один dummy-тест; ранее этих проверок не
было. Источник ожидаемых графов — определения внешнего архива; источник
веток нагета — RW из таблицы выше. **Ожидание сохранения dummy-слота
требует дополнительного источника RW** (S-7474), runtime его не подтвердил.

Статус **stop / draft**, не review. Владелец должен предоставить штатный
`Data/Scripts/Scripts.ini` либо полный штатный mount (RotWK + BFME2 +
Ennorath). После этого: уточнить RW-ожидание dummy-оружия, собрать свежую
основу, пройти весь `PORT-277*`, получить фактическую восьмистрочную
runtime-таблицу и только затем перевести PR из draft. Никаких выводов,
что все реальные кнопки работают или что range-finder не выпускает
Local 76, этот прогон не даёт.
