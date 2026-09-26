# Translations

User-visible C strings use gettext. English is supplied initially; no unreviewed machine translation is shipped.

From the repository root, regenerate the template:

```sh
xgettext --language=C --from-code=UTF-8 --keyword=_ --keyword=N_ \
  --package-name=galaxy-terminal --package-version=0.2.0 \
  --files-from=po/POTFILES --output=po/galaxy-terminal.pot
```

Create `po/LANGUAGE.po` from the template and add the language code to `po/LINGUAS`. Meson compiles and installs catalogs. Validate with `msgfmt --check po/LANGUAGE.po`. Translate visible labels; do not translate INI keys, profile palette IDs or shortcut key names. Desktop-entry translations can be added as standard `Name[LANGUAGE]` / `Comment[LANGUAGE]` entries. Documentation translations are welcome separately.
