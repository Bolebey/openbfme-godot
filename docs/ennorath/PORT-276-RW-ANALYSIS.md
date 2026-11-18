# PORT-276: targetless trap / mine detonation

Fork issue 643, dashboard task 255. Target: supplied RotWK 2.01 `game.dat`
(MD5 checked: `daed3668006cd90f01c34e5a7da1901f`). This identifies the
supplied artifact, not independently verified clean provenance; PLAN's
S-001 caveat still applies. The instructions in the RW branch map below
were read with `tools/mod/rw.py dis` during the original investigation;
BFME2 is a donor, not the target oracle. The follow-up source annotations
are identified separately and are not new disassembly evidence.

## Scope and the first-check gate

The issue explicitly requires checking the existing engine first and stopping
if a target next to a detonating trap already takes damage. **That gate was
reached: the full `openbfme_tests` passed both `PORT-276*` cases, 77 assertions,
on the unchanged engine from main `3260086f48221c29c2c02b12299f1af546fefb51`.**
The enemy took exactly the synthetic death weapon's damage and the trap was
removed, for both INITIAL and FINAL. No engine fix is justified by this
reproducer; work stops as instructed. The test uses synthetic INI, not an
installed game or mod.

`WeaponFireSpecialAbilityUpdate` itself belongs to Local 76. Its functions
have not been changed. The generated function / field / matrix maps have not
been changed either.

## Effective mod data (read locally, not copied into Git)

The chain is **not** a special weapon directly damaging the enemy:

- `SalemePoisonTrap`, `SalemePoisonTrapStrong`, `SalemeBlindTrap`, and
  `SalemeExplodeTrap` use `SalemeDetonateFallen` to kill themselves with a
  BURNED death. The trigger affects allies and self and has SUICIDE and
  DamageDealtAtSelfPosition. The enemy-facing effect comes from the selected
  `SlowDeathBehavior` weapon, not from this trigger's damage estimate against
  an enemy.
- The poison and blind traps run an INITIAL death weapon; the explosive trap
  runs a FINAL death weapon. The blind trap's death weapon applies emotion
  and attribute-modifier nuggets, not an ordinary damage nugget.
- `IsengardDeployedExplosiveMine` uses `IsegardDetonateMine` with the same
  self-kill mechanism, followed by its BURNED FINAL death weapon.
- Their OCL powers also make auxiliary objects. Both auxiliary templates
  (`SalemeDetonateRangedObj` and `IsengardDetonateRangedObj`) have a
  `FireWeaponUpdate` using `ArrowVolleyPhaseInitialWeapon`. That weapon has
  FireFX but no damage nugget. It does not replace the special weapon ->
  self-kill -> death weapon chain examined here.

Sources inspected: `data/ini/object/evilfaction/units/harad/salemetraps.ini`,
`data/ini/object/evilfaction/units/isengard/mine.ini`, `data/ini/weapon.ini`,
`data/ini/includes/weapon.inc`, both object-creation-list files,
`data/ini/object/projectile.ini`, `data/ini/object/system/system.ini`, and
`data/ini/specialpower.ini` in the effective Ennorath 1.9.8.3 archive.
No original INI text or copied numeric data is included here or in the tests.

## RotWK branch map and the existing port

| Entry (first instruction) | Branches read in RW | Existing port |
|---|---|---|
| RW 0x8961F7, `WeaponFireSpecialAbilityUpdate::triggerAbilityEffect` | 0x896220..0x896288: SuppressForHordes and horde-target redirection. 0x896288..0x89629D: weapon must exist and be ready. 0x8962A3..0x8962C0: set weapon owner ID; a victim uses the object overload. 0x8962D8..0x896313: without a victim, zero target position or NeedLivingTargets chooses `object + 0x38`, otherwise the stored position; call RW 0x6CF3D2. 0x896332..0x89636B: busy-until, base trigger, recharge. | `SpecialAbilityModules.cpp`, existing `triggerAbilityEffect`. No edits. |
| RW 0x6CF3D2, position force-fire wrapper | Pushes null victim and victim ID, the supplied position, ignoreRanges = true, special/projectile detonation = false, then calls RW 0x6CEF6D (0x6CF3D8..0x6CF3F0). This is the ordinary position-nugget path, not the projectile-detonation overload. | `ObjectWeapons::fireExtraWeaponAt`. |
| RW 0x6CEF6D, `Weapon::privateFireWeapon` | Bonus, template gate, optional assistance, leech deadline, ready gate (0x6CEF88..0x6CF010). Stream, rate / delay, ammo and barrel checks (0x6CF010..0x6CF092). Special branch (0x6CF092), linear-target branch (0x6CF0AE), scatter-list branch (0x6CF173), otherwise pass the position unchanged to RW 0x6CC915 (0x6CF259..0x6CF27B). Shot counters, ammo, barrel, reload / delay tail follow delivery (0x6CF280..0x6CF325). No alive-source veto here. | `Object/WeaponState.cpp`, `privateFireWeapon`. |
| RW 0x6CC915, `WeaponTemplate::fireWeaponTemplate` | Null source or both victim and position absent returns (0x6CC920..0x6CC93E). The supplied position is valid without a victim. Ignore-ranges or unexpired leech deadline skips range checks (0x6CCA2C..0x6CCAA1). Fire FX precede damage. DamageDealtAtSelfPosition selects the source position and clears the victim (0x6CCBF9..0x6CCC10); the GiantBird AI case keeps the target z (0x6CCC38..0x6CCC51). Miss / dodge / passenger handling and ScatterIndependently precede the nugget loop. A victim uses nugget slots 1 / 5 (0x6CCED3..0x6CCEF5); no victim uses slots 2 / 6 (0x6CCEFA..0x6CCF17). | `ObjectWeaponDelivery::fireWeaponTemplate`, existing self-position and radius branches. |
| RW 0x90DEF0, `DamageNugget` position effect | Radius is clamped to at least one (0x90DF0C..0x90DF1F). Source is looked up by weapon owner ID (0x90DF24..0x90DF30); dying is not the same as absent. 0x90DF35..0x90DF77 selects cylinder / bounding sphere / geometry overlap. Partition query at 0x90DFB9. For each object: arc test, applicability slot 1 (0x90E199..0x90E1A6), height caps, minimum radius, apply slot 14 (0x90E23A..0x90E243); the splash threshold may stop after the first successful hit. | `WeaponDelivery.cpp`, `radiusDamage`, `damageAreaDistance`. |
| RW 0x90D77C / RW 0x90E855, nugget applicability | Upgrade / self / similar / projectile / height / relationship / filter gates are in the base. `SELF` admits the source (0x90D7CA..0x90D7F6). Damage applicability fills a no-flank record, requires a victim and a findable source (0x90E874..0x90E8A8), checks leadership, estimates damage, and accepts positive damage **or a forced kill** (0x90E994..0x90E9A6). No effectively-dead-source gate. | Existing `shouldDeliver` and `damageApplicable`. Other unrelated base-nugget branches are outside this task. |
| RW 0x90E28C, `DamageNugget` fill | At 0x90E603..0x90E61A compare weapon source ID with victim ID, test RadiusDamageAffects bit 0x10 (SUICIDE), and set `DamageInfo + 0x24` (kill). The nugget's death type is preserved (0x90E641..0x90E647). Ordinary damage without SUICIDE does not force a kill. | `Object/Damage.cpp`, existing `FillDamageInfo`. |
| RW 0x90E683 / RW 0x698E7D, apply damage | Fill the record (0x90E6A9), HEALING takes its separate path, otherwise `Object::attemptDamage` (0x90E6C7). RW 0x698E7D defers one frame only when the **victim** has UPDATING_AI, or when the record has a positive delay; otherwise it applies immediately (0x698E7D..0x698EBA). | Existing `applyToVictim` and `Object::attemptDamage`. |
| RW 0x861712 / RW 0x860E93, slow-death selection / beginning | `SlowDeathBehavior` tests the death mask, marks AI dead once (0x861735..0x861751), collects eligible slow-death modules (0x8617B4..0x8617FC), picks a probability-weighted one and invokes its begin slot (0x861805..0x8618D6). `beginSlowDeath` sets absolute phase frames and active flag (0x8612AE..0x8612C9), then dispatches INITIAL (0x8612CD..0x8612D1). There is no new-alive-source requirement before that phase. | `CombatModules.cpp`, existing `onDie` / `beginSlowDeath`. |
| RW 0x8609A8, phase effect | FX, OCL, weapon, sound lists, in order. The phase weapon is selected with logic RNG; non-null weapon (0x860A4B..0x860A7B) calls RW 0x6CF530 with the dying object and `object + 0x38` (0x860A7D..0x860A8C). | Existing `SlowDeathBehavior::doPhase`. |
| RW 0x6CF530, temporary position weapon | Allocate primary-slot weapon, set owner ID, load ammo, set leech deadline to frame + 1, fire at the supplied position, release weapon (0x6CF53F..0x6CF582). No dead-source gate. | Existing `ObjectWeapons::createAndFireTempWeapon` / `fireTempWeapon`. |
| RW 0x860B39, slow-death update | On the destruction frame, dispatch FINAL (phase 2) **before** destroying the source: 0x860E50 `push 2`; 0x860E55 `call 0x8609A8`; 0x860E61 `call 0x62BBAB`. Thus the temporary weapon can still resolve its source while applying the blast. | Existing `SlowDeathBehavior::update`, same order. |

The map's unported class slots RW 0x896156 / RW 0x896370 are Local 76's
range / update work, not evidence that the existing position weapon cannot
deal damage. The generated map labels RW 0x8961F7 as ported already.

### BFME2 donor cross-check

Donor checkout: Open-BFME/Open-BFME-2 at
`7ff7192e924ce57a0d4e4c9a518151c247200c5a`; paths below are relative to `Code/`.

- DONOR BFME2 0x00492A72,
  `GameEngine/Source/GameLogic/Object/Update/WeaponFireSpecialAbilityUpdate.cpp`:
  no victim chooses stored position or the source, then `forceFireWeapon`.
- DONOR BFME2 0x002CE76F,
  `GameEngine/Source/GameLogic/Object/WeaponPrivateFireWrappers_O1.cpp`:
  position `forceFireWeapon` passes ignoreRanges and no victim.
- DONOR BFME2 0x002CE30A,
  `GameEngine/Source/GameLogic/Object/WeaponPrivateFireWeapon.cpp`:
  regular / special / linear / scatter dispatch, then timing and ammo tail.
- DONOR BFME2 0x00507D84,
  `GameEngine/Source/GameLogic/Object/DamageNugget.cpp`:
  position effect queries neighbours, then applies each eligible nugget.
- DONOR BFME2 0x0045D97A / 0x0045DB0B,
  `GameEngine/Source/GameLogic/Object/Behavior/SlowDeathBehaviorUpdate.cpp`:
  temporary death weapon at the source's position; FINAL before destruction.

SUICIDE is independently confirmed in RW 0x90E28C, not assumed from the
BFME2 damage-info donor (which differs here).

## Историческая проверка baseline на main `3260086`

Это фактические результаты первоначальной работы и контрольного прогона на
`89f48b525d6b55d003473c49eaffec08cce7f03d`, а не новая проверка после rebase
на `3d017a41`. Текущая граница проверки описана отдельно ниже.

`engine/tests/test_port_276.cpp`, included by `engine/cmake/Port276.cmake`
in the normal full test target:

1. Targetless OCL special power and WeaponFire update with a nonzero unpack
   delay, no normal weapon set: self-kill with BURNED death, INITIAL and FINAL
   blast variants, exact damage to an enemy inside the radius, no damage
   outside or to an ally, source gone afterward and blast attributed to its ID.
   The synthetic OCL is intentionally empty: the real auxiliary object's
   weapon is FX-only, as established above. The test does not replace firing
   or dying with a direct damage / destroy call.
2. Neighbour: remove SUICIDE, keep SELF and self-position delivery; a single
   ordinary wound to the source, no death weapon and no enemy damage.

Первоначальные тесты запущены **без какой-либо правки движка**: тогда
`git diff origin/main -- engine/src` был пуст, main был `3260086`. Между
исходным `a0302bf` и тем main добавились только light checks, не исследуемый
код. Исходный тестовый коммит — `f00cc4bc0700efdcfba4f1c97f0ee8dbd02ca196`;
после нового rebase он стал `89f1f866`. Эти два файла теста и lane побайтно
совпадают с историческим проверенным `89f48b5`.

В первоначальном sandbox выполнены полная Release-сборка штатной цели
`openbfme_tests` и её повторная сборка после подключения `Port276.cmake`,
обе с exit 0. Это не отдельный облегчённый runner. Тогда CMake / Ninja
были установлены в `.venv`; команды первоначального прогона:

```sh
export PATH="$PWD/.venv/bin:$PATH"
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBFME_BUILD_GODOT=OFF
cmake --build engine/build -j1 --target openbfme_tests
engine/build/openbfme_tests '-tc=PORT-276*' --no-colors
```

Фактический вывод исторического контрольного прогона на `89f48b5`, exit 0:

```text
[doctest] doctest version is "2.4.12"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:  2 |  2 passed | 0 failed | 6864 skipped
[doctest] assertions: 77 | 77 passed | 0 failed |
[doctest] Status: SUCCESS!
```

`6864 skipped` — остальные случаи полного бинарника, исключённые фильтром;
внутри двух PORT-276 кейсов нет skip и не нужны установки игры / мода.
Полный набор тестов и retail-сценарии не запускались. CMake отдельно
предупредил об отсутствующих `lame` / `mpg123` и MP3 fixtures: это ограничение
аудио-тестов, не провал или пропуск PORT-276. Своё CI не запускалось.

| Поведение baseline | Основание RW | PORT-276 проверка / результат |
|---|---|---|
| Targetless-подрыв в позиции владельца, BURNED self-kill | RW 0x8961F7 -> 0x6CF3D2 -> 0x6CC915; SUICIDE: RW 0x90E28C, 0x90E603..0x90E61A | Первый кейс, обе фазы: враг 200 -> 163, ловушка удалена, источник урона — её ID. |
| Радиус / отношения, ровно один взрыв | RW 0x90DEF0 / 0x90D77C | Первый кейс: дальний враг 200, союзник 200; ближайший теряет ровно 37, не 74. |
| INITIAL и FINAL до удаления источника | RW 0x860E93 (0x8612CD..0x8612D1), 0x8609A8, 0x860B39 (0x860E50..0x860E61) | Первый кейс: одинаковый точный урон и исчезновение ловушки для INITIAL / FINAL. |
| Без SUICIDE нет принудительного убийства и death weapon | RW 0x90E28C (0x90E603..0x90E61A) | Второй кейс: ловушка жива, 120 -> 117, враг даже внутри радиуса igniter остаётся 200, выстрел один. |

Видно в игре: **новой правки нет**, ручная проверка пяти настоящих кнопок не
выполнена; шаги ниже. Это диагностические тесты и их CMake-интеграция, не
местная или общая поведенческая правка. Нового состояния симуляции и изменений
`xfer` / `crc` / save нет. Чужие тесты, `perf1`, `uber1`, `save4*` и `.hashes`
не менялись и не переснимались; сдвиг их ожиданий не заявляется.

INFERENCE: the matrix may be asking the allies-only trigger for enemy damage
rather than observing its death weapon (especially the blind trap's non-damage
effect). This is a hypothesis from `SweepEffectProbe.h`, not a reason to
change the engine or the probe in this issue. No probe code has been edited.

## Границы и ручная проверка

Это проверка существующего пути, не объявление всех пяти кнопок исправленными.
Полный сценарий на установке мода и видимые / звуковые эффекты здесь не запускались.
Новых читателей INI из FIELD_GAPS и новых портированных RW-функций нет;
переход red -> green для матрицы не заявляется. Успешный baseline нельзя
выдавать за тест, падающий без новой правки. MATRIX, MATRIX_GAPS,
RW_FUNCTION_MAP и FIELD_GAPS не изменялись.

Существующий S-321 (порядок радиусных жертв: RW 0x90DEF0 -> partition query
0x90DFB9, в порте обход списка объектов) не закрывается: это общий механизм
всех радиусных орудий, а не изменение собственного класса / функции этой
задачи. Проверка на одной жертве внутри радиуса не устанавливает паритет
порядка нескольких жертв. Функциональный дубликат S-321 не создаётся;
новый S-7469 фиксирует только отказ по условию 5.7(б), не заменяя этот стоп.
Область Local 76 (включая RW 0x896156 / 0x896370 и собственную trigger-функцию
RW 0x8961F7) также остаётся за её владельцем.

Владельцу — одной партией, через Cheat Mode (`docs/CHEAT_MODE.md`):

1. Схватка Ennorath, Cheat Mode включён, вражеский компьютер. Esc -> CHEAT MODE
   -> Game: приостановить ИИ противника, чтобы он не строил и не уводил цель.
2. Units: дать противнику `GondorFighterHorde`. Selected: заморозить отряд и
   включить hold fire; это удерживает бойцов на месте и исключает обычный бой.
   Для слепящей ловушки перед подрывом снять заморозку, оставив hold fire,
   иначе DISABLED_HELD скроет реакцию движения на emotion-эффект.
3. На свободном месте рядом с бойцами создать для себя нужную ловушку из
   таблицы ниже. Оставить второй такой же вражеский отряд далеко от неё как
   контроль. Не замораживать саму ловушку: DISABLED_HELD блокирует способность.
4. Выделить ловушку, Selected -> сделать способности готовыми, затем нажать
   её кнопку из таблицы. Для мины дождаться завершения подготовки. Сравнить
   здоровье / эффект ближайших бойцов и дальнего контрольного отряда.
5. Повторить для каждого из пяти шаблонов на свежих целях: остаточный яд от
   предыдущей ловушки не должен смешиваться со следующим наблюдением.

| Свой объект | Кнопка | Что проверять по данным мода |
|---|---|---|
| `SalemeExplodeTrap` | `Command_SalemeExplosionsfalleDetonate` | Ловушка исчезает, FINAL-взрыв ранит ближайших врагов; дальний контроль цел. |
| `SalemePoisonTrap` | `Command_SalemeGiftfalleDetonate` | После самоподрыва INITIAL-оружие отравляет ближайших врагов; здоровье падает во времени. |
| `SalemePoisonTrapStrong` | `Command_SalemeGiftfalleDetonate` | Та же цепочка, усиленный яд. |
| `SalemeBlindTrap` | `Command_SalemeBlendfalleDetonate` | Проверять emotion / attribute-modifier эффект на ближайшем отряде, не требовать потери здоровья от оружия без DamageNugget. |
| `IsengardDeployedExplosiveMine` | `Command_IsengardMineDetonate` | По окончании подготовки мина исчезает; FINAL-оружие ранит ближайших врагов, дальний контроль цел. |

## Повторная проверка требований

После повторного запроса владельца, 2026-11-17:

- Восстановлена опубликованная история назначенной ветки на `89f48b5`:
  восстановленные рабочие файлы побайтно совпали с ней. Посторонняя CI-
  инфраструктура оказалась частью прежнего main, а не изменениями PORT-276;
  она не правилась и в diff задачи не входит.
- Выполнен fetch и rebase на main
  `3d017a419640f1876f1b41fbce75f49e4d9fbdb3`. Три собственных коммита стали
  `8c6638f9`, `89f1f866`, `be6ca64d`; исходные объекты `f00cc4b` и `89f48b5`
  по-прежнему доступны и их тип `commit` проверен через `git cat-file -t`.
- `git diff origin/main -- engine/src` пуст: runtime-код этой задачей не
  изменён. Сравнение **между main `3260086` и main `3d017a41`**, а не только
  diff собственной ветки, также показало побайтно неизменённые
  `Module/SpecialAbilityModules.cpp`, `Module/SpecialPowerModules.cpp`,
  `Module/CombatModules.cpp`, `Combat/WeaponDelivery.cpp`,
  `Object/WeaponState.cpp`, `Object/Damage.cpp`. Пути относительно
  `engine/src/GameLogic/`. В `Combat/ObjectWeapons.cpp` есть чужие изменения
  атак и проверки дальности, но блок overload-обёрток и временного оружия
  от `fireExtraWeapon` до `createAndFireTempWeaponAt` побайтно неизменён.
- Файлы `engine/tests/test_port_276.cpp` и `engine/cmake/Port276.cmake`
  побайтно совпадают с историческим проверенным `89f48b5`. Их ожидания при
  продолжении не менялись.

Это сравнение исходников, **не новый прогон** и не доказательство паритета
всех вызванных ими подсистем свежего main. После восстановления sandbox
прежние `.venv`, build, тестовый бинарник, RW-артефакты, донор и логи
отсутствуют. Ни сборка, ни `PORT-276*` на `3d017a41` заново не запускались;
новое чтение RW через `rw.py` также не заявляется. Отсутствие установленных
CMake / Ninja не выдаётся за ошибку или невозможность сборки. Решение
остановиться основано на явном первом gate issue и отсутствии доказанной
разницы в разрешённом пути, а не на нехватке компилятора.

### Соседнее поведение и конкретное условие отказа

При чтении свежего исходника
`WeaponFireSpecialAbilityUpdate::triggerAbilityEffect` встречен комментарий
о невыполненном вызове FiringTracker: RW 0x896210 внутри entry RW 0x8961F7,
RW 0x8E302A(1), `Object + 0x248`. **Это существующая аннотация порта, не новая
дизассемблировка и не подтверждение семантики всей системы tracker.** Сама
trigger-функция запрещена для изменения в PORT-276: это Local 76. Рядом
также остаётся отмеченная в исходнике передача SPECIAL_WEAPON от horde к
членам в `startUnpacking` (аннотации RW 0x895ECE / 0x6944A5, S-859): та же
чужая область. Общий порядок радиусного обхода остаётся S-321.

Для этих мест провалено **условие PROCESS 5.7(б): не общий механизм и не
чужая область**. Размер возможной правки, (г), и отсутствие сдвига пинов,
(в), для запрещённой реализации не утверждаются. Ни маленький размер вызова,
ни соседство с детонацией не отменяют (б). Оформлен **S-7469** — стоп границы
задачи, не новый функциональный дубликат S-184 / S-321 / S-859. Эти пробелы
не названы причиной пяти NO-EFFECT; обходного хука и собственного теста
несделанного переноса нет. Полный аудит или перенос класса Local 76 не
заявляется.

### Приёмка повторного запроса

| Требование | Фактический результат | Остаток / стоп |
|---|---|---|
| Существующий целый путь, RW, без молчаливых defaults | Исторически прочитана цепочка targetless fire -> self-position -> SUICIDE/BURNED -> INITIAL/FINAL -> radius -> destroy, адреса и ветки в RW map выше. Синтетический INI проходит через читатель движка, runtime не заменён вторым путём. | Новый отличающийся шаг не доказан; вся система и пять реальных кнопок не объявляются исправленными. |
| Допустимое соседнее поведение со своим тестом или явный стоп | Существующий соседний тест без SUICIDE прошёл исторически. Найденные при чтении пропуски в чужих функциях и общем обходе не переносились. | S-7469: не выполнено (б). Тест без SUICIDE не доказывает tracker / horde / partition. |
| Хотя бы один настоящий red→green плюс соседний тест | Соседний диагностический тест есть; оба новых кейса уже зелёные без runtime-правки: 2/2, 77/77. | **Не выполнено:** red→green от новой правки нет, S-7470. Изъять для красного прогона нечего; искусственно ломать baseline нельзя. |
| Английский upstream-коммит как PR description | Подтверждённой новой vanilla-правки нет; результат verdict-only не проходит upstream gate. | **Не выполнено:** upstream-ветка, коммит и ready-строка не подготовлены, S-7470. Fork-коммит с отчётом не считается upstream deliverable. |

При появлении подтверждённого дефекта upstream-описание должно включать
эффект для игрока, поведение RotWK с инструкциями, затронутые настоящие
vanilla-шаблоны / редакции при необходимости, реальный red→green и Pins.
Сейчас нечем честно заполнить эффект **новой** правки и переход от её
удаления. Upstream API, перенос на upstream/main и работа всех редакций не
проверялись и не заявляются; пустая ready-ветка не создаётся.

### Карты и ожидания

| Категория | Было -> стало в PORT-276 |
|---|---|
| Новые runtime-читатели INI / портированные RW-функции | Нет -> нет; новых данных симуляции, xfer / crc / save нет. |
| Настоящий red→green | Не установлен -> не установлен, S-7470. Два добавленных диагностических кейса уже проходили на main. |
| Чужие тесты и пины | Не изменены; никакие ожидания не пересняты, новый сдвиг не заявляется. |
| MATRIX, MATRIX_GAPS, RW_FUNCTION_MAP, FIELD_GAPS | Не изменены; свип не перезапускался, его NO-EFFECT не переобозначены как PASS. |
| Новые стопы и журнал | S-7469 и S-7470 в собственных фрагментах; обновлён только собственный night-watch. S-7471 свободен в резерве задачи. |

## Итог и «Осталось»

Первый check issue зелёный уже на историческом main: **остановка по прямому
условию issue, а не заявленный новый порт**. Повторный запрос не дал
воспроизведения отличающегося шага и сохранил запрет Local 76. Runtime-код,
тесты и пины при продолжении не менялись. Отчёт и стопы явно показывают
невыполненные red→green / upstream-требования; статус не `review`.

Fork PR не открыт. Upstream PR / `up/port-276` не подготовлены: результат
verdict-only, новой подтверждённой поведенческой правки vanilla нет.
Upstream-ready ветка или PR, все пять работающих кнопок и сборка свежего
main не заявляются.

Осталось: владельцу дать воспроизведение, которое падает на main в
разрешённом пути, либо явно разрешить PR **только с диагностикой**, без
заявления об исправлении. Пять настоящих кнопок можно проверить партией
Cheat Mode выше. Если NO-EFFECT сохраняется на полном моде, нужны входные
условия различия; причина в свипе пока только INFERENCE. Чинить его либо
чужую функцию в рамках этой остановки без разрешения нельзя.
