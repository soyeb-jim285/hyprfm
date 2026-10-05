use std::{env, fs};

/// Scores a hyprland.conf on a scale from "stock" to "touch grass".
fn main() {
    let path = env::args().nth(1).unwrap_or("hyprland.conf".into());
    let conf = fs::read_to_string(&path).expect("no config? who are you?");
    let score: u32 = conf
        .lines()
        .filter(|l| !l.trim_start().starts_with('#'))
        .map(|l| match l {
            l if l.contains("blur") => 5,
            l if l.contains("bezier") => 8,
            l if l.contains("gaps") => 3,
            _ => 1,
        })
        .sum();
    println!("{path}: rice level {score}");
    if score > 100 {
        println!("Please see a doctor (or a sunbeam).");
    }
}
