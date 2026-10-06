# Localization

[Forge wiki](../../GUIDE.md) · **Localization**

Translations, parameters, plurals, fallback, and reactive text are shared by the native runtime and UI modules.

- [Catalogs and translation API](#catalogs-and-translation-api)

## Catalogs and translation API

C++ localization is shared by Python and UI modules. Translations are separate from scenes; language changes need no restart. Standard English and Russian menu strings are embedded and available without translation files. Projects may override any key and add languages.

Add the optional "locales": "locales" group to paths and configure:

```json
"localization": {
  "default_language": "ru",
  "fallback_language": "en",
  "auto_detect": false,
  "save_selection": true,
  "warn_missing": true,
  "languages": {
    "ru": {"name": "Russian", "file": "ru.json"},
    "en": {"name": "English", "file": "en.json"},
    "fr": {"name": "Français", "file": "fr.json"}
  }
}
```

file is relative to paths.locales; directories and filenames may be changed. A language may use shorthand "fr": "fr.json". An object without file registers a language using its parent/fallback dictionary. name appears in settings. ru and en are always registered; translate engine.* keys for other languages to localize the entire menu. Embedded keys are listed in engine/include/forge/localization_data.hpp.

Without localization, existing projects work with English as the default and fallback; translation files are unnecessary. The bundled engine.json selects Russian; change default_language or switch at runtime for your game. default_language and fallback_language must be registered.

Example locales/ru.json (Russian text intentionally retained to demonstrate plural forms):

```json
{
  "menu": {"start": "Начать игру"},
  "hello": "Привет, {name}!",
  "inventory": {
    "one": "{count} предмет",
    "few": "{count} предмета",
    "many": "{count} предметов",
    "other": "{count} предмета"
  },
  "literal": "Запись {{name}} содержит фигурные скобки"
}
```

Example locales/en.json:

```json
{
  "menu.start": "Start game",
  "hello": "Hello, {name}!",
  "inventory": {"one": "{count} item", "other": "{count} items"},
  "literal": "The entry {{name}} contains braces"
}
```

Nested keys and dotted keys are equivalent; duplicates after flattening are rejected. Values may be strings, key groups, or plural objects zero/one/two/few/many/other; other is mandatory. Arrays and numeric dictionary values are rejected.

```python
import forge, ui

forge.log(forge.tr('hello', name='Anya'))
forge.log(forge.tr('inventory', count=21))  # Uses the Russian singular category for 21.

# A translation reference: Label/Button automatically update text and layout.
heading = ui.Label(forge.message('hello', name='Anya'), size=40)
button = ui.Button(forge.message('menu.start'), start_game)
forge.set_language('en')
assert str(heading.value) == 'Hello, Anya!'

# tr() returns a plain string containing the translation at the time of the call.
fixed = forge.tr('menu.start')
```

Substitution accepts named JSON parameters such as {name}; names contain ASCII letters, digits, or underscores and cannot start with a digit. {{ and }} produce literal braces. Missing parameters raise an exception; extra parameters are accepted. Strings insert directly; other JSON values insert as JSON. Regional date/currency/number formatting, ICU MessageFormat, nested conditions, and printf specifiers are not implemented: prepare the parameter in a game module.

Pass a numeric count for plural selection; strings, bool, NaN, and infinity are unsupported. The engine embeds Unicode CLDR 48 **cardinal** rules for 224 language/region entries. Selection uses the rule of the dictionary supplying the translation, including fallback. Integer 1 and numeric 1.0 can select different forms: JSON preserves this distinction, but not arbitrary trailing zeros such as 1.00. Negative count is classified by magnitude while the original value is inserted. Compact forms such as '1 million' and ordinals are unsupported; CLDR operands c/e are zero. For a custom language code, set "plural_language": "ru" or "other" (one form). An unknown rule with plural messages fails validation. Rule source: [Unicode CLDR](https://www.unicode.org/cldr/charts/48/supplemental/language_plural_rules.html); data and license ship in the repository and build.

| API | Purpose |
| --- | --- |
| `tr(key, **params)` | Return a string in the current language |
| `message(key, **params)` | Create a reactive LocalizedText reference |
| `LocalizedText(key, params={})` | Same with an explicit parameter dict; str(value) translates when called |
| `language()` | Current normalized language code |
| `set_language(code, persist=True)` | Switch language and linked entities; unknown language raises without changing state |
| `available_languages()` | List of {'code': ..., 'name': ...} for a selector |
| `has_translation(key, language='', fallback=True)` | Check without warning; empty language means current |
| `localization_revision()` | Language/dictionary change counter for custom components |

Codes normalize to lowercase with underscores replaced by hyphens: EN_gb → en-gb. Selecting ru-RU falls back to registered ru when no separate regional catalog exists. Lookup follows selected code → parents (en-gb → en) → fallback and parents → default and parents. An absent translation returns its key; warn_missing logs WARN once per language/key until catalogs reload. Fallback use is also diagnosed; using a parent catalog is not an error.

auto_detect uses the first preferred macOS language or Windows user language, choosing a registered code or its parent. With no match, default_language remains. When save_selection is enabled, a saved selection overrides system/default choices. MenuController.show_settings() automatically adds a language selector. Selection is stored in `<save_directory>/preferences/language.json`; corrupt/outdated preferences are ignored with WARN. persist=False changes the current language but does not cancel an earlier pending persistent selection. save_selection=False disables preference reads/writes.

Preferences use a temporary file and atomic replacement after successful scene/frame processing or at shutdown. Failed scene initialization restores language and pending selection without writing the rejected scene's choice.

JSON entity text can be linked directly:

```json
{"id": "greeting", "kind": "text", "text_key": "hello", "text_params": {"name": "Anya"}}
```

Python entities support entity.text = forge.message('hello', name='Anya') and entity.set_localized_text('hello', {'name': 'Anya'}). text_key and text_params are separately accessible; parameters return a copy that must be reassigned after editing. text returns the current translation. Assigning a plain string clears its translation binding. Use set_localized_text to validate a key/parameter pair atomically; a manually created invalid pair raises when read or refreshed. LocalizedText.params also returns a copy.

Dialogue accepts forge.message(...) for lines and speakers, retaining keys/parameters in history. Changing language keeps a fully revealed line revealed; a partial line retains its revealed character count within the new length. capture/restore preserves history keys and the fully-revealed flag; plain string dialogue remains supported. Free text, player names, and titles of old saves do not translate automatically. For custom serialized data, store a key and JSON parameters rather than a LocalizedText instance.

C++ modules can call forge::active->localization.translate(key, params). A standalone forge::Localization loads with load(config) and provides translate, select, languages, has, and flush. After manually changing active C++ localization, refresh entities through forge::active->refreshLocalizedEntities(); Python set_language handles this atomically.

validate checks dictionaries, plural categories, braces, paths, and matching named-parameter sets for shared keys across languages. Complete translation coverage is not required because fallback applies. Editing translation JSON in dev reloads the scene and catalogs; failure preserves language, dictionaries, objects, and pending selection. build and init copy the configured locale directory; the game never downloads CLDR.

Language selection does not add glyphs to a font: choose renderer.font and fallback_fonts with the required characters ([cameras, uvs, postprocessing, and uniforms](rendering.md)). The renderer supports fallback fonts but lacks bidi/RTL shaping, ligatures, and complex-script layout. Arabic/Hebrew dictionaries can be stored and queried, but correct display requires a text backend extension. Localization does not automatically switch images, voice-over, or other assets; a game module can do so using forge.language().
