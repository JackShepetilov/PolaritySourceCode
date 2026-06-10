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
- Панель UnrealClaude в редакторе: **Tools → Claude Assistant**. AI Chat VibeUE требует ключ
  vibeue.com — для MCP он НЕ нужен.

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
  Read. ⚠️ Содержимое скиллов писано под v4: брать оттуда **концепции и подводные камни**, но
  НЕ имена методов — сигнатуры только из `get_help` и фактических ответов наших `manage_*`.

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
