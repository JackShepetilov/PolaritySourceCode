# MCP-тулинг редактора (VibeUE + UnrealClaude) — наша конфигурация

Источник инсайтов: чужой working-agreement под **UE 5.7 + VibeUE v4** (оригинал вербатим:
`Polarity/Docs/VibeUE_UE57_WorkingAgreement_original.md`). У нас другие версии, поэтому файл
адаптирован и сверен с фактически установленными плагинами (2026-06-10):
**UE 5.6.1, VibeUE v3.0 (ветка `5-6-community-contribution`), UnrealClaude v1.5.0.**

---

## 1. Наша конфигурация (проверено по факту установки)

- **VibeUE v3** — stdio python-сервер (`Plugins/VibeUE/Python/vibe-ue-main/Python/.venv`) → TCP
  `127.0.0.1:55557`. Листенер в редакторе поднимается автоматически (editor subsystem `UBridge`).
  MCP-инструменты (action-based, у каждого множество `action`):
  `check_unreal_connection`, `get_help(topic)`, `manage_asset`, `manage_blueprint`,
  `manage_blueprint_variable`, `manage_blueprint_component`, `manage_blueprint_node`,
  `manage_blueprint_function`, `manage_umg_widget`.
  Справка встроена: `get_help(topic="topics")` → список тем (blueprint-workflow, node-tools,
  umg-guide, troubleshooting, properties, asset-discovery, node-positioning, multi-action-tools).
- **UnrealClaude v1.5** — node-мост (`mcp-bridge`) → HTTP `127.0.0.1:3000` (поднимается модулем
  плагина на PostEngineInit). Инструменты: `spawn_actor`, `move_actor`, `delete_actors`,
  `set_property`, `get_level_actors`, `open_level`, `asset_search`, `asset_dependencies`,
  `asset_referencers`, `capture_viewport`, `get_output_log`, `blueprint_query`; мега-роутер
  `unreal_ue` (домены blueprint / anim / character / enhanced_input / material / asset); скрытые
  (вызываемые, но не листятся): `task_*`, `execute_script`, `run_console_command`.
- Оба сервера живут в `Source/.mcp.json`. Им нужен **запущенный редактор с загруженными
  плагинами**: без него handshake пройдёт, но любой tool-вызов упадёт по соединению.
- ⚠️ **UnrealClaude лжёт про успешный старт**: при занятом порте 3000 в логе будет
  `LogHttpListener: Error: HttpListener unable to bind to 127.0.0.1:3000`, но строкой ниже модуль
  всё равно напишет «MCP Server started on http://localhost:3000» (проверено 2026-06-10). Порт
  захардкожен (`UnrealClaudeConstants.h: DefaultPort = 3000`, constexpr), перезапуск сервера без
  рестарта редактора невозможен (нет ни консольной команды, ни UCLASS-сабсистемы). Диагностика:
  `netstat -ano | findstr :3000` + грэп лога по `LogHttpListener`. Лечение: освободить порт и
  перезапустить редактор; при хронических коллизиях — сменить константу и пересобрать плагин
  (+ `UNREAL_MCP_URL` в `.mcp.json`).
- API-ключ vibeue.com на TCP-мост 55557 НЕ влияет: предупреждение `LogMCPServer: API key not
  configured` касается другого компонента (HTTP MCP/чат); `get_system_info` по TCP отвечает без
  ключа (проверено живьём).
- ⚠️ **`execute_script` = модальный диалог «Execute PYTHON Script?» в редакторе.** Вызов уходит
  в task queue (`task_submit` → `task_status`/`task_result`), а диалог разрешения **блокирует
  весь редактор, включая сам HTTP-сервер** — все запросы (даже `task_status`) висят по таймауту,
  пока человек не кликнет Approve. Это штатно, не дедлок. Скрипты сохраняются в
  `Content/UnrealClaude/Scripts/*.py` (чистка — тул `cleanup_scripts`). В конце результата
  бывает `[WARNING: ... no new actors were created ...]` — ложнопозитивный шум для
  ассет-скриптов, игнорировать.
- Панель UnrealClaude в редакторе: **Tools → Claude Assistant**. AI Chat VibeUE требует ключ
  vibeue.com — для MCP он НЕ нужен.
- ✅ **Прямой REST к редактору (проверено живьём 2026-06-10):** `GET http://127.0.0.1:3000/mcp/tools`
  (полный список из 28 тулов, включая скрытые), `POST http://127.0.0.1:3000/mcp/tool/<имя>`
  (JSON-тело = аргументы). Так зовутся скрытые тулы в обход MCP-клиента.
  **`run_console_command` БЕЗ диалогов** (прямой `GEditor->Exec` + захват вывода; вывод python
  уходит в LogPython, поле `output` ответа может быть пустым — проверять через `get_output_log`).
  Это рабочий канал для `py "<скрипт>.py" <args>` — т.е. сборка арен `build_arena.py` запускается
  автономно при ОТКРЫТОМ редакторе (headless при открытом редакторе запрещён — конфликт
  сохранения). Валидатор команд: запрещены `;`/`|`/`&&`/бэктики/`$(`/`${` (= только запуск
  .py-файлов, однострочники с `;` не пройдут), блок-префиксы `open`, `exec`, `obj`, `mem`, `net`,
  `quit`, `r.`, `gc.`, `stat slow` и др., лимит 2048 символов. Команда выполняется синхронно на
  game thread — редактор «висит» на время скрипта, для сборки арены это нормально.
- **`open_level` (exposed тул)** = штатный `UEditorLoadingAndSavingUtils::LoadMap`. Вне PIE
  безопасен; при несохранённых изменениях покажет модалку «Save?» (кликает человек, редактор и
  HTTP висят до клика). Перед переключением уровней: убедиться что PIE остановлен и всё сохранено.
- **`snap_level.py`** (`Source/Tools/ArenaBlockout/`) — скриншоты ТЕКУЩЕГО открытого уровня без
  его сохранения (транзиентный SceneCapture2D, как в build_arena.py): авто-фрейминг топ + 4 изо
  (приоритет BLOCKOUT-тегам, иначе median-фильтр XY-выбросов; sanity-cap 200k uu отсекает
  Ultra_Dynamic_Sky и пр.), опциональный аргумент — JSON с кастомными ракурсами
  (`[{"id","pos","pitch","yaw","fov"}]`). Запуск через run_console_command:
  `py "<...>/snap_level.py" [shots.json]` → PNG в `Tools/ArenaBlockout/Build/Screenshots/<Level>_<id>.png`.

## 1.0a PythonAPI-сервисы: проверенные грабли (2026-06-11, живьём)

Полный цикл «материал + Blueprint c графом» собран чистым python через `run_console_command`
(см. `Tools/ArenaBlockout/make_containment_field.py` — рабочий образец):
- Имена полей структур отличаются от C++: `MaterialNodePinInfo.name` (НЕ `pin_name`),
  `BlueprintPinInfo.pin_name`/`.is_input` (НЕ `b_is_input`), `BlueprintCompileResult.num_errors`/
  `.errors`/`.warnings` (НЕ `error_count`). Перед использованием новой структуры — `dir()`-probe.
- Одновходовые материальные ноды (OneMinus/Saturate) НЕ принимают пустое имя пина в
  `connect_expressions` — их вход зовётся `Input`, выходы `Output_0` (бери из `get_expression_pins`).
- `BlueprintService.set_node_pin_value` на **объектные** пины (SourceMaterial и т.п.) — молчаливый
  no-op (false). Обход: назначать материал на ИНСТАНСЕ актора после спавна, CDMI без SourceMaterial
  берёт текущий материал слота.
- `set_component_property` НЕ выставляет `bNotifyRigidBodyCollision` на SCS-шаблоне — включать
  нодой `SetNotifyRigidBodyCollision` в BeginPlay.
- Событие `ReceiveHit` уже имеет пин `HitLocation` — `BreakHitResult` не нужен. `GetTimeSeconds`
  ждёт коннект `WorldContextObject` (подключай любой компонент).
- `add_variable` → ОБЯЗАТЕЛЬНО `compile_blueprint` ДО создания нод с этой переменной (правило скилла).

## 1.1 Локальные патчи плагинов (повторить при обновлении плагина!)

Плагины из `Plugins/` участвуют в сборке таргета PolarityEditor (первый же Build/Live Coding
после их появления инвалидирует makefile). UHT требует уникальности **имён хедеров** и **имён
рефлектируемых типов** во всём таргете, поэтому поверх замороженной 5.6-ветки VibeUE живут наши
патчи (2026-06-11):
- `Chat/ChatTypes.h` → `Chat/VibeUEChatTypes.h` (+ `.generated.h` внутри, + 5 инклюдов) — коллизия
  имени файла с `Source/Polarity/Variant_Shooter/Stream/ChatTypes.h`;
- `FChatMessage` → `FVibeUEChatMessage` (79 вхождений, 13 файлов чата) — коллизия engine-имени с
  DataTable-структом `FChatMessage` из `Source/Polarity/ChatMessageTypes.h` (его переименовывать
  НЕЛЬЗЯ — сломаются таблицы внутриигрового чата);
- `UnrealClaude.uplugin`: `EngineVersion` 5.7.0 → 5.6.0.

Первая сборка плагинов в проектном контексте — ~200 действий; Live Coding с его лимитом
(`-LiveCodingLimit=100`) её не вытягивает — гонять `Build.bat` при закрытом редакторе. После
этого Live Coding для .cpp-правок работает как обычно.

## 2. ЧЕГО У НАС НЕТ (в оригинале есть — не использовать вслепую)

Оригинал писан под VibeUE **v4**: там основной интерфейс — `execute_python_code` + Services
(`WidgetService`, `ScreenshotService`, `MaterialNodeService`, `BlueprintService`, …) и
`manage_skills`. **В нашей v3 этого нет.** Также нет тулов с префиксом `unreal_*`
(`unreal_capture_viewport`/`unreal_move_actor` из оригинала = наши `capture_viewport`/`move_actor`).

- Произвольный python в редакторе у нас возможен предположительно только через скрытый
  `execute_script` UnrealClaude (или `run_console_command` + `py`) — **семантику проверить при
  первом использовании**, не переносить рецепты вслепую.
- **Скиллы у нас ЕСТЬ, но не через MCP**: 29 каталогов
  `Plugins/VibeUE/Content/Skills/<имя>/skill.md`. «Загрузить скилл» = прочитать файл обычным
  Read. Скиллы из 5.6-ветки документируют **нашу** v3 (скилл `materials` сверен живьём
  2026-06-10: вызовы статические `unreal.MaterialService.create_material(...)`, результаты —
  объекты с `.asset_path`/`.id`/`.success`). Новый скилл при первом использовании всё равно
  сверяй пробным вызовом.
- **Сервисы PythonAPI подтверждены живым тестом** (2026-06-10, end-to-end через
  `execute_script(python)`): `unreal.MaterialService` + `unreal.MaterialNodeService` создали
  материал, собрали граф (2 параметра → BaseColor/Roughness), `compile=True`,
  `export_material_graph` показал обе output-связи, ассет удалён. Биндинг `unreal.*Service`
  работает — это рабочий путь ко всем ~28 сервисам из `Source/VibeUE/Public/PythonAPI/`.

**Маппинг домен → скилл** (читать ПЕРЕД первой правкой в домене):

| Триггер / префикс ассета | Скилл |
|---|---|
| BP_, Blueprint, переменные, компоненты | `blueprints` |
| ноды, граф, пины, wire/connect | `blueprint-graphs` |
| M_, MI_, материалы | `materials` / `landscape-materials` |
| WBP_, UMG, HUD | `umg-widgets` |
| IA_, IMC_, Enhanced Input | `enhanced-input` |
| DT_ / DA_ | `data-tables` / `data-assets` |
| ST_, StateTree | `state-trees` |
| скриншоты / vision | `screenshots` |
| расстановка акторов в уровне | `level-actors` |
| скелет / AnimBP / монтажи / секвенции | `skeleton` / `animation-blueprint` / `animation-montage` / `animsequence` / `animation-editing` |
| ландшафт / террейн | `landscape` / `landscape-auto-material` / `terrain-data` |
| Niagara / VFX | `niagara-systems` / `niagara-emitters` |
| звук | `sound-cues` / `metasounds` |
| enum/struct, теги, настройки проекта | `enum-struct` / `gameplay-tags` / `project-settings` / `engine-settings` |
| поиск/перенос ассетов | `asset-management` / `foliage` |

## 3. Железные правила (engine-level, от версии не зависят)

1. **PIE блокирует редактирование Blueprint'ов.** Любая правка графа/переменных во время Play
   возвращает пустой GUID / "Editor is currently in a play mode". Заверши PIE и подожди 2–3 с.
   После старта PIE — 5–8 с до первого наблюдения.
2. **Крэш-классы тулинга.** (a) world-lifecycle вызовы через тулинг во время PIE (`open_level`,
   level travel, `quit_editor`) — hard-crash: рестарты/переходы уровней триггерить только из BP
   самой игры (кнопка → OpenLevel), никогда из editor-python. (b) Тяжёлые синхронные операции
   (FBX-импорт через `import_asset_tasks`) из скриптового контекста — assertion в TaskGraph и
   крэш: откладывать на slate post-tick callback (рецепт — §1.15 оригинала). (c) Runtime-спавн
   акторов через тулинг нестабилен — предпочитать пул pre-placed акторов (совпадает с нашим
   правилом «editor-placed refs вместо runtime spawn»).
3. **Восстановление после крэша редактора:** убить зависшие `UnrealEditor` +
   `CrashReportClientEditor`, перезапустить, ждать 60–120 с. **НИКОГДА не удалять
   `Binaries/`/`Intermediate/`** ради «починки» — это длинный missing-modules ребилд.
4. **Скриншоты врут.** Вьюпорт с выключенным realtime НЕ перерисовывается — повторные
   `capture_viewport` возвращают байт-в-байт старый кадр («не сработало» ≠ правда). Сдвинь камеру
   (`set_level_viewport_camera_info`) и подожди 3–5 с после изменений сцены. UMG/Slate во
   вьюпорт-скриншот НЕ попадает.
5. **Серый материал = fallback всего материала.** Несоответствие SamplerType ↔ compression
   текстуры роняет весь материал в дефолтный серый, при этом compile возвращает success.
   Соответствия: BaseColor → sRGB+TC_DEFAULT+Color; Normal → noSRGB+TC_NORMALMAP+Normal;
   одноканальные rough/metal/AO → noSRGB+TC_GRAYSCALE+LinearGrayscale; packed ORM →
   noSRGB+TC_MASKS+Masks. Диагностика: BaseColor → Emissive; цвет не меняется вообще = fallback,
   а не освещение.
6. **Enhanced Input съедает замапленные клавиши:** raw `InputKey` для клавиши из активного
   контекста никогда не сработает. Новый дискретный контрол = новое IA + маппинг (клавишу сначала
   убрать из других экшенов; индексы маппингов сдвигаются — удалять с конца).
7. **ChildActorComponent ловушки:** ребёнок без явного парента цепляется под отмасштабированный
   рут и наследует scale (иерархия это скрывает — смотри world transform); запись
   `ChildActorClass` в pre-placed инстансы уровня не доходит (читается только из шаблона). Для
   повторяемого контента — отдельные акторы, расставленные в edit-time.
8. **success=True от тула — НЕ доказательство.** В оригинале задокументированы молчаливые no-op:
   `bind_event` для UMG-кнопок, `set_component_property` для FName/class-свойств. После каждой
   правки: перечитать состояние (`get_*`/inspect), скомпилировать BP, проверить 0 ошибок — только
   потом заявлять «готово». Это наше правило «no unverified claims», применённое к MCP.
9. **Сохранение:** `save_asset` по каждому затронутому ассету;
   `EditorLoadingAndSavingUtils.save_dirty_packages(True, True)` для «сохранить всё»
   (`save_dirty_packages_with_dialog` в EditorAssetLibrary не существует). После изменения
   class-defaults (CDO) обязателен compile + save, иначе PIE держит старое значение.
10. **Лог — твой единственный undo.** Авто-отката нет: печатать `CREATED:/MODIFIED:/DELETED:` +
    полный путь после каждой мутации; вывод BP-логики читать в `Saved/Logs/Polarity.log`
    (`get_output_log` у UnrealClaude).

## 4. Цикл верификации (адаптирован под наши тулы)

1. Прочитай скилл домена (Read `Content/Skills/<имя>/skill.md`) + `get_help` по теме.
2. Правка: VibeUE `manage_blueprint*` / `manage_umg_widget` / `manage_asset`; уровень и акторы —
   UnrealClaude (`spawn_actor`, `move_actor`, `set_property`, `unreal_ue`).
3. Compile BP (action в `manage_blueprint`) → 0 ошибок; перечитай граф/свойства (`blueprint_query`,
   `manage_blueprint_node` get-actions) — убедись, что задуманная связка реально существует.
4. PIE: запускается/останавливается из редактора; ждать снаружи (PowerShell `Start-Sleep`), не
   sleep'ом внутри editor-python (заморозит game thread).
5. Наблюдение: `capture_viewport` (3D-сцена; помни про правило 4), `get_output_log` /
   `Saved/Logs/*.log` для PrintString/UE_LOG.
6. Конец PIE + 2–3 с — только потом следующая правка BP.

---

*Если какой-то рецепт из оригинала нужен дословно (deferred FBX-импорт, walk widget-tree,
delegate-bind для кнопок и т.д.) — открой архив и перепроверяй каждое имя API через наши тулы:
половина сигнатур там v4-специфична.*
