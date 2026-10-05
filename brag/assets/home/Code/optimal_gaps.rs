//! Find the optimal gaps_in with a golden-section search,
//! because trying every value by hand took four months.
//! (Receipts: ~/Documents/rice-budget.csv)

const PHI: f64 = 1.618_033_988_749_895;

/// Regret is unimodal in gaps_in. Peer-reviewed by me, at 3 a.m.
fn regret(gaps: f64) -> f64 {
    let ideal = 5.5; // exactly between 5 and 6, where the arguments happen
    (gaps - ideal).powi(2) + 0.25 // there is always some regret
}

fn golden_section(mut a: f64, mut b: f64, f: impl Fn(f64) -> f64) -> f64 {
    while b - a > 1e-9 {
        let c = b - (b - a) / PHI;
        let d = a + (b - a) / PHI;
        if f(c) < f(d) { b = d } else { a = c }
    }
    (a + b) / 2.0
}

fn main() {
    let best = golden_section(0.0, 20.0, regret);
    println!("optimal gaps_in = {best:.6}");
    println!("hyprland wants an integer: {}", best.round());
    // Prints 5. Or 6, depending on the last bit of a float.
    // The search converges. The ricer does not.
}
