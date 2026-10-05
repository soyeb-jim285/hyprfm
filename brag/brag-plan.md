# Brag Plan: HyprFM

Two cuts from one set of real recordings: **short** (~24 s, social) and **long** (~88 s, feature tour).

## What is this app?
A fast, keyboard-friendly Qt6/QML file manager for Hyprland and Wayland. Every file previews itself inline (code through bat, Markdown through md4c, PDF pages, fonts, archives, photo EXIF, music tags, video posters), and everything is reachable without a mouse.

## The angle
**A nature documentary about a Hyprland ricer.** The ricer has riced everything: bar, gaps, blur, colours. Everything except the file manager. The demo home *is* the character: wallpapers named `great-wave-but-dark.jpg`, cats named `code-reviewer.jpg`, a `todo.md` that reads "[x] change gaps_in from 5 to 6 / [x] change gaps_in from 6 to 5 / [ ] actually finish thesis", and `thesis-final-FINAL-v3.pdf` titled "On the Optimal Value of gaps_in". HyprFM is the thing that finally matches the rice, and it keeps exposing the ricer's life one preview at a time. The punchline lands on the thesis: you can now find it, preview it, delete it, and Ctrl+Z it back. You still have to write it.

Narration is on-screen text in a calm documentary voice (serif, lower-left), against fast, real UI.

## Hook (first 2–3 s)
Long: the ricer's `todo.md` rendered in HyprFM's preview, the camera pushed in on "[x] change gaps_in from 5 to 6 / [x] change gaps_in from 6 to 5". Caption: "The Hyprland ricer. Habitat: ~/".
Short: the empty desktop, HyprFM snaps open. Caption: "You riced everything. Except this."

## Key moments
- Miller columns: the preview pane follows the arrow keys through Cosmic Cliffs, the Great Wave, Starry Night (real keys, real frames).
- Inline previews of everything: Rust highlighted, `todo.md` rendered, the thesis PDF, `dotfiles-from-reddit.zip` contents, a font specimen.
- Space quick preview: a cat photo with its real camera EXIF, then arrow through the other cats and ENIAC without closing it; music tags ("Music To Rice To"), a Saturn V poster, thesis pages.
- 10 themes reloading live from `config.toml`.

## Outro / punchline
Long: delete the thesis, Ctrl+Z restores it. "HyprFM can restore your thesis. It can't write it." → logo, install line.
Short: themes cycle → logo, `yay -S hyprfm-git`.

## User flow worth showing
Open a folder → arrow through files and watch each one preview → Space to inspect any file in detail → act on it (copy across a split, rename in bulk, undo).

## Tone
- Preset: default (long leans deadpan)
- Creative direction: "nature documentary about a Hyprland ricer"
- Interpretation: dry, affectionate narration; the UI moves fast and real, captions hold still and long enough to read; jokes come only from the demo files the ricer owns.

## Format: landscape — 1920x1080, 30 fps
## Duration: short ~24 s, long ~88 s (user asked for long form; overrides the 15–25 s rule for that cut only)

## Visual identity (from the project)
- Background: #11111b (Catppuccin Mocha crust; the app's default theme), panels #1e1e2e
- Accent: #89b4fa (blue), secondary #cba6f7 (mauve), #f5c2e7 (pink, from the logo)
- Text: #cdd6f4
- Display font: a serif for narration (documentary voice); body: Adwaita Sans (the app's UI font in the recordings); mono: JetBrains Mono for keycaps
- Strongest visual element: the real window, framed with rounded corners and depth; keycaps that appear exactly when the real key was pressed (from the capture key log)

## Grounded claims (and where they come from)
- "Opens in 163 ms": docs/performance-history.md (launch 171 → 163 ms).
- bat-highlighted code, md4c Markdown, PDF via poppler, archive listing, font specimen: src/services/previewservice.cpp.
- Camera EXIF / music tags / video info inline: src/services/metadataextractor.cpp.
- Space quick preview, browse with arrows: QuickPreview.qml, configmanager.cpp:173.
- F3 split, Ctrl+Alt+arrows pane focus, Ctrl+L path bar, F2 bulk rename, Ctrl+Z undo of trash: configmanager.cpp defaults, undomanager.cpp.
- Git status badges: gitstatusservice.cpp.
- 10 bundled themes, live reload on config.toml save: themes/, configmanager.cpp file watcher.
- Install: AUR `hyprfm-git`, Flatpak, AppImage, Nix (README).
Everything else (the ricer, thesis, cats) is framing from the demo files, not product claims.

## Share copy (draft)
HyprFM: a file manager for people whose todo list says "change gaps_in from 5 to 6". Miller columns, Space previews anything, 10 live-reloading themes, and you never touch the mouse.

## Audio direction
- Role: warm bed under a dry documentary voice; key presses audible but soft.
- Music: long → `happy-beats-business-moves-vol-9` (114.8 BPM, laid back); short → `vol-10` (110 BPM, punchy).
- Music treatment: fade in over 1 s, duck slightly under dense key passages, fade under the final logo; logo lands on a strong cue.
- Music cue guidance: vol-9 strong cues 6.34, 10.54, 12.65 s (chapter turns); vol-10 strong cues 20.19, 20.74 s (short logo). Chapter cards snap to the beat grid; keycaps follow the real key log, not the grid.
- Audio-reactive treatment: subtle; bass makes the background glow behind the window breathe. No visualizers.
- SFX posture: moderate; one randomized keypress sound per logged key (low volume), soft drop on caption cards, one bell on the logo.
- Restraint rule: no SFX on theme swaps beyond a soft switch; never stack more than two sounds.

## Storyboard — long (~88 s)
1. Cold open — 6 s. `tour` clip, segment where `todo.md` renders; push-in on the checklist. Caption: "The Hyprland ricer." then "Habitat: ~/". Audio: music fades in.
2. Launch — 6 s. `launch` clip, window appears. Caption: "Its file manager opens in 163 ms." (from docs). Logo bug + "HyprFM" lower-third.
3. Miller — 9 s. `wallpapers` clip; keycaps ↓ from key log. Caption: "Miller columns. The preview follows your arrow keys."
4. Everything previews — 15 s. `tour` clip; captions change on the key log: "Rust, highlighted by bat." → "Markdown, rendered." → "PDFs, inline." → "Archives, opened without opening." → "Fonts, specimen included."
5. Quick preview — 10 s. `peek` clip. Caption: "Space peeks at anything." then "Camera EXIF included. The cat did not consent."
6. Media — 9 s. `peek_media`. Caption: "Music tags. Video posters. PDF pages."
7. Two panes, no mouse — 7 s. `split`. Caption: "F3 splits. Copy across without touching the mouse."
8. Path bar — 4 s. `pathbar`. Caption: "Ctrl+L, with suggestions."
9. Bulk rename — 5 s. `rename`. Caption: "Rename in bulk, with a live preview."
10. Git — 4 s. `git`. Caption: "Git status, right in the list."
11. Themes — 6 s. `themes`, `theme = "…"` card updates in sync. Caption: "10 themes. Save config.toml, it reloads."
12. Undo / punchline — 6 s. `undo`. Caption: "Deleted the thesis?" → "Ctrl+Z." → "HyprFM can restore your thesis. It can't write it."
13. Outro — 6 s. Logo, "HyprFM", `yay -S hyprfm-git`, "AUR · Flatpak · AppImage · Nix", repo URL. Bell on strong cue.

## Storyboard — short (~24 s)
1. Hook — 3 s. `launch`. "You riced everything." → "Except this."
2. Reveal — 3 s. Window glides right; logo + "A fast, keyboard-friendly file manager for Hyprland."
3. Miller — 5 s. `wallpapers`. "Every file previews itself."
4. Peek — 5 s. `peek`. "Space peeks at anything."
5. Themes — 4 s. `themes`. "10 themes. Live reload."
6. Outro — 4 s. Logo, `yay -S hyprfm-git`, URL. Logo on vol-10 strong cue.

**Music mood:** documentary-warm (long), upbeat (short).
**Audio summary:** a calm bed that lets real key clicks carry the rhythm, swelling into the logo.
