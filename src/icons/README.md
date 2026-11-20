# Strata icon pack

The built-in **Strata** pack ports all 117 interface/category SVGs and all 354
Catppuccin file-type SVGs from Strata, with its filename, compound-extension,
category fallback and filename-pattern associations. The SVG source files are
copied unchanged. Their CSS color variables are resolved before Qt renders them.

Select **Settings → Look & Feel → Icon Pack → Strata**. New configurations use it
by default; saved desktop/system icon-pack choices remain valid. **File icon
colors** selects **Brand colors** (Catppuccin Mocha on dark backgrounds, Latte on
light backgrounds) or **Theme accent**. Folders and standard interface icons use
the accent; semantic warning/error colors are preserved. Views, search, recents,
previews, file dialogs and drag images share the same rendering rules. Image,
video and PDF thumbnails retain their content.

The equivalent config is:

```toml
[general]
icon_theme = "Strata"
file_icon_colors = "brand" # or "theme"
```

The palette threshold, translucent folder fill, stroke scaling and file-icon
palette resolution follow Strata's `src/assets.rs`. Compatible HyprFM QML icon
types wrap the bundled Lucide artwork; controls with no Strata counterpart keep
their existing vector icon. All imported SVGs can also be requested by their
`strata-*` names from the icon image provider. System application logos continue
to resolve through installed icon packs.

Regenerate the bundle and QML adapters with:

```sh
python3 scripts/import-strata-icons.py ~/Projects/strata
```

Source: Strata commit `8f12b46324b519a58a289549d36f9d4b2e421d4c`.

- Lucide 1.35.0: ISC, Lucide and Feather contributors; see
  [Lucide-ISC.txt](strata/licenses/Lucide-ISC.txt).
- Catppuccin VSCode Icons v1.26.0, revision
  `b6915da9f6889b683a110aa747de96c2820a537d`: MIT, copyright 2023 Catppuccin
  and thang-nm; see [Catppuccin-VSCode-Icons-MIT.txt](strata/licenses/Catppuccin-VSCode-Icons-MIT.txt).
- Strata's original artwork, associations and adaptation: MIT, copyright 2026
  LGSE Ltd.; see [Strata-MIT.txt](strata/licenses/Strata-MIT.txt).

All notices are embedded with the resources and installed under
`share/hyprfm/licenses/strata/`. The bundle needs no external icon-pack download.
