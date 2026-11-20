# Bundled themes

HyprFM includes its ten original themes and all 95 themes from Strata's bundled
catalog. Imported themes use `strata-<id>` names, including `strata-tokyo-night`,
`strata-azure-glow`, `strata-catppuccin-frappe`, and `strata-nord-light`.
Select one in Settings → Appearance, or set `theme = "strata-tokyo-night"` under
`[general]` in `~/.config/hyprfm/config.toml`. They also work as `light_theme`
and `dark_theme` choices.

## Source and updates

The initial import uses `data/themes/catalog.toml` from Strata commit
`8f12b46324b519a58a289549d36f9d4b2e421d4c`. Each generated theme records the
catalog's SHA-256. To regenerate from a local Strata checkout (Python 3.11+):

```sh
python3 scripts/import-strata-themes.py ~/Projects/strata
python3 scripts/import-strata-themes.py ~/Projects/strata --check
```

Strata's catalog includes original palettes and palettes derived from
[Tinted Theming schemes](https://github.com/tinted-theming/schemes), revision
`fdca32a0d14ec80ad83a78a9ccb85592ca6cb9e1`, with foreground corrections recorded
in Strata's catalog. Preserve the [Strata MIT notice](licenses/Strata-MIT.txt)
and [Tinted Theming MIT notice](licenses/Tinted-Theming-MIT.txt) when distributing
these themes. Both notices are installed alongside the theme files.

## Color mapping

Strata and HyprFM expose different semantic tokens. The port preserves Strata's
background, panel, text, accent, and error colors, and derives the additional
HyprFM text and background levels:

| HyprFM token | Strata source |
| --- | --- |
| `base`, `crust` | `background` |
| `mantle` | Midpoint between `background` and `surface` |
| `surface` | `surface` |
| `overlay` | `border` |
| `text` | `text` |
| `muted` | Strata's readable `dim_text` |
| `subtext` | Midpoint between readable `dim_text` and `text` |
| `accent` | `accent` |
| `success` | `syntax_string` |
| `warning` | `syntax_type` |
| `error` | `danger` |

The dim-text adjustment follows Strata's two-pass blend toward body text:
first reach 4.5:1 contrast on the background, then 3:1 on the surface. HyprFM
computes selection and hover fills from the accent/text colors; Strata's
separate `highlight` and background `muted` tokens are not used as text colors.
HyprFM's preview syntax highlighting remains controlled by its preview tools.

Every imported theme includes a derived 16-color terminal palette. ANSI colors
0–7 use background, danger, syntax string, syntax type, accent, syntax keyword,
syntax preprocessor, and text. Colors 8–15 use border followed by the same seven
foreground colors. These are derived from Strata's semantic/syntax catalog;
the catalog does not supply the original schemes' full ANSI palettes.
