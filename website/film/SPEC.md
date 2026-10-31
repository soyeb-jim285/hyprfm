GOAL:    a Hyprland user watches the real HyprFM window work over a fog valley at dusk and wants to install it.
OUTPUT:  HyperFrames, 1920 x 1080, 30 fps, silent (it autoplays muted on the website hero).
TIME:    35 s x 30 = 1050 frames.
LOOK:    ground: the photograph "Vallee-brouillard" (Thomas Bresson, CC BY 3.0), cover, anchored top so the
         apricot sky is the top 250 px; it drifts scale 1.06 -> 1.0 over the whole film.
         ink #1c2130 on the sky (contrast > 7:1 on #f2b877); no accent except the logo's own blue.
         display: Wix Madefor Display 500, titles 60 px, tracking -0.022em; the reveal wordmark 132 px.
         command: JetBrains Mono 500 38 px.
         medium: the real HyprFM window, captured with Hyprland blur over this same photo, cropped to the
         window (2176 x 1376 source) and shown at 1240 x 784, radius 20 px, shadow 0 40 80 -24 rgba(8,10,20,.55).
OBJECTS: the HyprFM window (real captures only), the fog valley, the HyprFM logo file (dist/*.svg).
MOTION:  arrive expo.out 0.5-0.7 s; camera power2.inOut 0.8 s; exits power3.in 0.3 s.
         titles: whole line blur 8 -> 0 in 0.18 s, no translate; out blur + fade 0.2 s.
         window content swaps: blur swap (out 0.15 s to 8 px, in 0.15 s from 8 px), one whip (x 120 px + 14 px blur, 0.28 s),
         one hard cut on a punch-in, a fog flood when the light theme lands, the window shrinking into the logo.
SHOTS:   0.0-6.3   views    Miller steps through photos; starts punched 1.5x on the preview pane, pulls back at 2.4.
                            "Walk your files by keyboard" 0.3-2.3, then the wordmark reveal HyprFM 3.4-6.0.
         6.3-10.8  preview  Space opens quick preview, arrows browse; "Look without opening"; punch on the photo at 8.6.
         10.8-14.8 docs     PDF page, then rendered Markdown; whip in; "PDF and Markdown, rendered"; punch on the pane at 12.9.
         14.8-17.2 split    a second pane opens; "Two folders, side by side".
         17.2-20.8 rename   bulk rename preview; hard cut in, punched on the dialog at 18.9; "Rename the whole selection".
         20.8-24.6 terminal F4 shell, git status; "A shell under every folder"; punch on the terminal at 22.7.
         24.6-29.0 light    Rose Pine Dawn, Miller through photos; fog flood; "Dark or light, your theme".
         29.0-30.9 claim    window settles; "Made to live on Hyprland".
         30.6-35.0 end card window shrinks into the logo; HyprFM wipes in beside it; `yay -S hyprfm-git` types in; hold 2.4 s.
BANNED:  dark void ground; glow, particles; glass cards; title over a dimmed screenshot; logo docked in a corner;
         every element fading up; titles that move after landing; drawn UI.
QA:      0.0 (thumbnail: punched-in photo preview, not black), 2.0, 4.5 (wordmark), 8.0, 12.0, 16.0, 19.5, 23.5,
         27.0, 30.0, 33.5 (end card readable, command full).
