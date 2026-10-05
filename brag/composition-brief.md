# Hyperframes Composition Brief: HyprFM

## Objective
Two launch videos for HyprFM from one set of real recordings: a ~26 s social cut and a ~89 s feature tour.

## Output
- Generator: `brag/composition/build.mjs` + edit lists in `brag/composition/edl.mjs` → one standalone HyperFrames composition per cut (built in CI by `brag/ci/render.sh`)
- Rendered: `hyprfm-long.mp4`, `hyprfm-short.mp4` (GitHub Actions artifact `videos`), music muxed afterwards by `brag/ci/mux-music.sh`
- Format: landscape 1920x1080, 30 fps

## Source Material
- Project root: /home/jim/hyprfm (HEAD of `brag-video`, built from source in CI)
- Recordings: the real HyprFM binary in headless sway, driven by `wtype`, recorded by `wf-recorder -D` (`brag/capture/takes.sh`); each take logs its key presses
- Demo home: `brag/assets` (public-domain/CC0 media, credits in `brag/assets/CREDITS.md`)
- Copy that must appear: "A fast, keyboard-friendly file manager for Hyprland." (README), `yay -S hyprfm-git`, "AUR · Flatpak · AppImage · Nix", repo URL

## Creative Direction
- Tone preset: default (long cut leans deadpan)
- Creative direction: nature documentary about a Hyprland ricer
- Angle / hook / punchline: see `brag/brag-plan.md`
- Avoid: generic SaaS language, filler visuals, invented product claims

## Visual Identity
- Background #11111b, panel #1e1e2e, text #cdd6f4, accent #89b4fa, pink #f5c2e7, green #a6e3a1 (Catppuccin Mocha, the app's default theme)
- Narration: Instrument Serif; labels: Inter; keycaps/code: JetBrains Mono (local woff2 from @fontsource)
- The real window, framed at 85% with rounded corners and depth; captions and keycaps share the band under it

## Audio
- Role: warm bed under real key clicks
- Music: long → happy-beats vol-9, short → vol-10 (bundled with /brag, not committed to the public repo; muxed locally), fade in 1 s, fade out 2.5 s, mix normalized to −14 LUFS
- Audio-reactive: per-frame RMS of the chosen track (`music-<cut>.rms.json`) drives the blue glow behind the window (opacity/scale), sampled every 2 frames
- SFX: one randomized CC0 keypress per logged key (per character for typed text), soft drop on chapter labels, a switch per theme change, soft impact on titles, bell on the outro logo; overlapping sounds are allocated to free tracks
- Beat sync: keycaps follow the real key log, not the beat grid (truth over rhythm); chapter changes are not beat-locked

## Gate
`npx hyperframes check` passes for both cuts (0 errors, all text WCAG AA). Lint warnings are only `nested_structure_needs_subcomposition` (Studio layout advice).
