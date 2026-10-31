hl.config({
    general = { gaps_in = 10, gaps_out = 40, border_size = 0, layout = "dwindle" },
    decoration = {
        rounding = 14,
        rounding_power = 2,
        shadow = { enabled = true, range = 70, render_power = 3, color = "rgba(07091099)", offset = { 0, 26 } },
        blur = { enabled = true, size = 10, passes = 3, noise = 0.012, brightness = 0.92, vibrancy = 0.2, new_optimizations = true },
    },
    animations = { enabled = true },
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true, force_default_wallpaper = 0 },
})
hl.curve("out", { type = "bezier", points = { {0.16, 1}, {0.3, 1} } })
hl.animation({ leaf = "windowsIn", enabled = true, speed = 4, bezier = "out", style = "popin 94%" })
hl.animation({ leaf = "windowsOut", enabled = true, speed = 3, bezier = "out", style = "popin 94%" })
hl.animation({ leaf = "fade", enabled = true, speed = 4, bezier = "out" })
hl.animation({ leaf = "workspaces", enabled = false })
hl.window_rule({ name = "fm", match = { class = "^(hyprfm)$" }, float = true, size = { "1360", "860" }, center = true })
