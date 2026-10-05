// Build a HyperFrames composition for one cut of the HyprFM brag video.
//
//   node build.mjs <cut: long|short> <takes-dir> <out-dir>
//
// The takes are real recordings of HyprFM (brag/capture/takes.sh). Each take
// has a key log ("<seconds> <kind> <what>") written while it was recorded, so
// keycaps and keypress sounds land on the frames where the real key was sent.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { CUTS } from './edl.mjs';

const [cutName, TAKES, OUT] = process.argv.slice(2);
const HERE = path.dirname(new URL(import.meta.url).pathname);
const cut = CUTS[cutName];
if (!cut) throw new Error(`unknown cut ${cutName}`);

// ---- key logs ----------------------------------------------------------
const logs = {};
function log(take) {
  if (!logs[take]) {
    const f = path.join(TAKES, `${take}.keys`);
    logs[take] = fs.existsSync(f)
      ? fs.readFileSync(f, 'utf8').trim().split('\n').filter(Boolean).map(l => {
          const [t, kind, ...rest] = l.split(' ');
          return { t: +t, kind, what: rest.join(' ') };
        })
      : [];
  }
  return logs[take];
}
// Time of the n-th event in a take matching kind/what (what may be a prefix).
export function ev(take, kind, what, n = 0) {
  const hits = log(take).filter(e => e.kind === kind && (what == null || e.what === what));
  if (!hits[n]) throw new Error(`no event ${take}:${kind}:${what}#${n}`);
  return hits[n].t;
}
const takeDuration = take => +execFileSync('ffprobe', ['-v', 'error', '-show_entries', 'format=duration',
  '-of', 'csv=p=0', path.join(TAKES, `${take}.mp4`)]).toString().trim();

// ---- layout constants (1920x1080) --------------------------------------
const W = 1920, H = 1080;
const SRC_W = 1600, SRC_H = 1000;         // logical size of the recordings
const WIN_S = cut.winScale ?? 0.85;       // window scale on the canvas
const WIN_W = SRC_W * WIN_S, WIN_H = SRC_H * WIN_S;
const WIN_X = (W - WIN_W) / 2, WIN_Y = cut.winY ?? 46;
const BAND_Y = WIN_Y + WIN_H;             // lower band for captions + keycaps

// ---- resolve scenes onto the timeline -------------------------------------
const scenes = cut.scenes(ev, takeDuration);
let t = 0;
for (const s of scenes) { s.start = +t.toFixed(3); t += s.dur; }
const TOTAL = +t.toFixed(3);

const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const KEYNAME = { Right: '→', Left: '←', Up: '↑', Down: '↓', space: 'Space', Return: 'Enter',
  Escape: 'Esc', Delete: 'Del', ctrl: 'Ctrl', alt: 'Alt', shift: 'Shift' };
const keyLabel = k => KEYNAME[k] ?? (k.length === 1 ? k.toUpperCase() : k);

const html = [], js = [], audio = [];
let sfxIdx = 0, aid = 0;
const KEYS = fs.readdirSync(path.join(HERE, 'sfx')).filter(f => f.startsWith('keypress-'));
function sfx(file, at, vol, dur = 0.4) {
  if (at < 0 || at >= TOTAL - 0.05) return;
  aid++;
  audio.push(`<audio id="sfx${aid}" src="assets/sfx/${file}" data-start="${at.toFixed(3)}" data-duration="${Math.min(dur, TOTAL - at).toFixed(3)}" data-track-index="${11 + (aid % 6)}" data-volume="${vol}"></audio>`);
}
const keySound = (at, vol = 0.32) => sfx(KEYS[(sfxIdx++ * 7) % KEYS.length], at, vol, 0.3);

const takesUsed = new Set();
let capN = 0, kcN = 0;

for (const [i, s] of scenes.entries()) {
  const id = `s${i}`;
  if (s.take) {
    takesUsed.add(s.take);
    // the recording, trimmed to [s.in, s.in + s.dur)
    html.push(`<video id="${id}v" class="vid clip" src="assets/takes/${s.take}.mp4" muted playsinline data-start="${s.start}" data-duration="${s.dur}" data-media-start="${s.in.toFixed(3)}" data-track-index="1"></video>`);
    // punch-ins: [{t, scale, ox, oy, d}] in scene time, origin in source px
    for (const z of s.zoom ?? []) {
      js.push(`tl.to('#zoom', {scale:${z.scale}, transformOrigin:'${z.ox}px ${z.oy}px', duration:${z.d ?? 0.8}, ease:'${z.ease ?? 'power2.inOut'}'}, ${(s.start + z.t).toFixed(3)});`);
    }
    if (!(s.zoom ?? []).length) js.push(`tl.set('#zoom', {scale:1}, ${s.start});`);
    // cut accent: the window breathes in on every scene change
    if (i > 0) js.push(`tl.fromTo('#win', {scale:0.985}, {scale:1, duration:0.45, ease:'power3.out'}, ${s.start});`);
    // keycaps + key sounds from the take's key log
    if (!s.noKeys) for (const e of log(s.take)) {
      const at = s.start + (e.t - s.in);
      if (e.t < s.in - 0.05 || e.t >= s.in + s.dur - 0.15) continue;
      if (e.kind === 'theme' || e.kind === 'launch') continue;
      let caps;
      if (e.kind === 'key') caps = [keyLabel(e.what)];
      else if (e.kind === 'chord') caps = e.what.split('+').map(keyLabel);
      else if (e.kind === 'type') caps = [`<span class="typed">${esc(e.what)}</span>`];
      if (!caps || e.what === 'Shift_L') continue;
      kcN++;
      const hold = Math.min(0.9, s.start + s.dur - at - 0.05);
      if (hold < 0.25) continue;
      html.push(`<div id="kc${kcN}" class="keys clip" data-start="${at.toFixed(3)}" data-duration="${hold.toFixed(3)}" data-track-index="4"><div class="keys-in">${caps.map(c => `<span class="kc">${c.startsWith('<') ? c : esc(c)}</span>`).join('<span class="plus">+</span>')}</div></div>`);
      js.push(`tl.fromTo('#kc${kcN} .keys-in', {y:14, opacity:0}, {y:0, opacity:1, duration:0.14, ease:'power3.out'}, ${at.toFixed(3)});`);
      js.push(`tl.to('#kc${kcN} .keys-in', {opacity:0, y:-6, duration:0.18, ease:'power2.in'}, ${(at + hold - 0.18).toFixed(3)});`);
      if (e.kind === 'type') for (let c = 0; c < e.what.length; c++) keySound(at + c * 0.07, 0.24);
      else keySound(at);
    }
    // theme lines (themes take): config.toml card follows the real edits
    for (const e of log(s.take).filter(e => e.kind === 'theme')) {
      const at = s.start + (e.t - s.in);
      if (at < s.start || at >= s.start + s.dur - 0.1) continue;
      kcN++;
      const next = log(s.take).filter(x => x.kind === 'theme' && x.t > e.t)[0];
      const until = Math.min(s.start + s.dur, next ? s.start + (next.t - s.in) : s.start + s.dur);
      html.push(`<div id="th${kcN}" class="toml clip" data-start="${at.toFixed(3)}" data-duration="${(until - at).toFixed(3)}" data-track-index="5"><div class="toml-path">~/.config/hyprfm/config.toml</div><div class="toml-line"><span class="tk">theme</span> = <span class="ts">"${esc(e.what)}"</span></div></div>`);
      sfx('switch_002.ogg', at, 0.22, 0.3);
    }
  } else {
    // title scene: html authored in the EDL, animated by its own tweens
    html.push(`<section id="${id}t" class="title clip" data-start="${s.start}" data-duration="${s.dur}" data-track-index="2">${s.html}</section>`);
    for (const a of s.anim ?? []) js.push(a.replace(/@(\d+(\.\d+)?)/g, (_, n) => (s.start + +n).toFixed(3)));
    // the window steps aside while a title owns the frame
    js.push(`tl.to('#win', {opacity:0, scale:0.96, duration:0.35, ease:'power2.in'}, ${s.start});`);
    js.push(`tl.to('#win', {opacity:1, scale:1, duration:0.45, ease:'power3.out'}, ${(s.start + s.dur).toFixed(3)});`);
    for (const x of s.sfx ?? []) sfx(x.file, s.start + x.t, x.vol ?? 0.6, x.d ?? 1.5);
  }
  // documentary captions: [{t, d, text, chapter}] in scene time
  for (const c of s.caps ?? []) {
    capN++;
    const at = s.start + c.t;
    const dur = Math.min(c.d, TOTAL - at);
    html.push(`<div id="cap${capN}" class="cap clip" data-start="${at.toFixed(3)}" data-duration="${dur.toFixed(3)}" data-track-index="3">${c.chapter ? `<div class="chapter">${esc(c.chapter)}</div>` : ''}<div class="line">${c.text}</div></div>`);
    js.push(`tl.fromTo('#cap${capN} .line', {y:22, opacity:0}, {y:0, opacity:1, duration:0.45, ease:'power3.out'}, ${(at + 0.02).toFixed(3)});`);
    if (c.chapter) js.push(`tl.fromTo('#cap${capN} .chapter', {x:-16, opacity:0}, {x:0, opacity:1, duration:0.35, ease:'power2.out'}, ${at.toFixed(3)});`);
    js.push(`tl.to('#cap${capN} .line, #cap${capN} .chapter', {opacity:0, duration:0.25, ease:'power1.in'}, ${(at + dur - 0.27).toFixed(3)});`);
    if (c.chapter) sfx('drop_001.ogg', at, 0.3, 0.4);
  }
}

// ---- audio-reactive glow (precomputed music RMS, optional) ---------------
let rms = [];
const rmsFile = path.join(HERE, `music-${cutName}.rms.json`);
if (fs.existsSync(rmsFile)) rms = JSON.parse(fs.readFileSync(rmsFile, 'utf8'));
if (rms.length) {
  js.push(`const RMS=${JSON.stringify(rms)};`);
  js.push(`for (let f = 0; f < RMS.length && f / 30 < ${TOTAL}; f += 2) tl.to('#glow1', {opacity: 0.30 + 0.30 * RMS[f], scale: 1 + 0.06 * RMS[f], duration: 2/30, ease: 'none'}, f / 30);`);
}
// slow ambient drift, independent of music
js.push(`tl.fromTo('#glow2', {x:-60, y:20}, {x:80, y:-30, duration:${TOTAL}, ease:'sine.inOut'}, 0);`);
js.push(`tl.fromTo('#glow1', {x:40}, {x:-50, duration:${TOTAL}, ease:'sine.inOut'}, 0);`);

// ---- assemble ------------------------------------------------------------
const page = `<!doctype html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=${W}, height=${H}" />
<title>HyprFM — ${cutName}</title>
<script src="assets/gsap.min.js"></script>
<style>
@font-face { font-family: "Instrument Serif"; src: url(assets/fonts/instrument-serif-400.woff2) format("woff2"); font-weight: 400; }
@font-face { font-family: "Instrument Serif"; src: url(assets/fonts/instrument-serif-400-italic.woff2) format("woff2"); font-weight: 400; font-style: italic; }
@font-face { font-family: "Inter"; src: url(assets/fonts/inter-500.woff2) format("woff2"); font-weight: 500; }
@font-face { font-family: "Inter"; src: url(assets/fonts/inter-800.woff2) format("woff2"); font-weight: 800; }
@font-face { font-family: "JetBrains Mono"; src: url(assets/fonts/jetbrains-mono-600.woff2) format("woff2"); font-weight: 600; }
:root { --crust:#11111b; --mantle:#181825; --base:#1e1e2e; --surface:#313244; --text:#cdd6f4; --sub:#a6adc8;
  --blue:#89b4fa; --mauve:#cba6f7; --pink:#f5c2e7; --green:#a6e3a1; --yellow:#f9e2af; }
body { margin:0; background:var(--crust); color:var(--text); font-family:Inter, sans-serif; }
#root { position:relative; width:100%; height:100%; overflow:hidden; background:var(--crust); }
.glow { position:absolute; border-radius:50%; filter:blur(110px); }
#glow1 { width:1100px; height:760px; left:${W / 2 - 550}px; top:${WIN_Y + WIN_H / 2 - 380}px; background:radial-gradient(closest-side, rgba(137,180,250,.85), rgba(137,180,250,0)); opacity:.35; }
#glow2 { width:900px; height:700px; left:${W - 760}px; top:${H - 520}px; background:radial-gradient(closest-side, rgba(203,166,247,.7), rgba(203,166,247,0)); opacity:.30; }
#grain { position:absolute; inset:0; opacity:.16; background-image:radial-gradient(rgba(205,214,244,.16) 1.2px, transparent 1.4px); background-size:34px 34px; }
#win { position:absolute; left:${WIN_X}px; top:${WIN_Y}px; width:${WIN_W}px; height:${WIN_H}px; border-radius:20px; overflow:hidden;
  background:var(--base); box-shadow:0 60px 140px rgba(0,0,0,.6), 0 0 0 2px rgba(205,214,244,.10); }
#zoomwrap { position:absolute; left:0; top:0; width:${SRC_W}px; height:${SRC_H}px; transform-origin:0 0; transform:scale(${WIN_S}); }
#zoom { position:absolute; left:0; top:0; width:${SRC_W}px; height:${SRC_H}px; }
.vid { position:absolute; left:0; top:0; width:${SRC_W}px; height:${SRC_H}px; object-fit:cover; }
.cap { position:absolute; left:${WIN_X}px; top:${BAND_Y + 20}px; width:${WIN_W - 520}px; }
.cap .chapter { font-family:"JetBrains Mono", monospace; font-weight:600; font-size:20px; letter-spacing:3px; color:var(--blue); text-transform:uppercase; margin-bottom:8px; }
.cap .line { font-family:"Instrument Serif", serif; font-size:${cut.capSize ?? 52}px; line-height:1.08; color:var(--text); }
.cap .line em { color:var(--pink); }
.cap .line code { font-family:"JetBrains Mono", monospace; font-size:.72em; color:var(--green); }
.keys { position:absolute; right:${WIN_X}px; top:${BAND_Y + 30}px; }
.keys-in { display:flex; gap:10px; align-items:center; justify-content:flex-end; }
.kc { font-family:"JetBrains Mono", monospace; font-weight:600; font-size:30px; color:var(--text); min-width:64px; height:64px; padding:0 18px;
  box-sizing:border-box; display:inline-flex; align-items:center; justify-content:center; border-radius:14px;
  background:linear-gradient(#3b3d54, #2a2b3d); box-shadow:0 7px 0 #0b0b12, 0 14px 30px rgba(0,0,0,.45); }
.kc .typed { color:var(--yellow); }
.plus { font-family:"JetBrains Mono", monospace; font-size:26px; color:var(--sub); }
.toml { position:absolute; right:${WIN_X + 24}px; top:${WIN_Y + WIN_H - 170}px; width:600px; padding:20px 28px; border-radius:16px;
  background:rgba(17,17,27,.94); box-shadow:0 24px 60px rgba(0,0,0,.6), 0 0 0 2px rgba(205,214,244,.14); font-family:"JetBrains Mono", monospace; }
.toml-path { font-size:20px; color:var(--sub); }
.toml-line { font-size:34px; margin-top:8px; color:var(--text); white-space:nowrap; }
.tk { color:var(--blue); } .ts { color:var(--green); }
.title { position:absolute; inset:0; display:flex; flex-direction:column; align-items:center; justify-content:center; text-align:center; }
.title .logo { width:150px; height:150px; filter:drop-shadow(0 0 34px rgba(137,180,250,.45)); }
.title h1 { font-family:Inter, sans-serif; font-weight:800; font-size:150px; letter-spacing:-5px; margin:18px 0 0; line-height:1; }
.title .sub { font-family:"Instrument Serif", serif; font-size:58px; color:var(--sub); margin-top:18px; max-width:1300px; line-height:1.1; }
.title .sub em { color:var(--pink); }
.title .term { font-family:"JetBrains Mono", monospace; font-weight:600; font-size:46px; margin-top:44px; padding:22px 44px; border-radius:18px;
  background:var(--mantle); box-shadow:0 0 0 2px rgba(205,214,244,.12), 0 30px 70px rgba(0,0,0,.5); }
.title .term .p { color:var(--green); }
.title .also { font-family:Inter, sans-serif; font-weight:500; font-size:34px; color:var(--sub); margin-top:34px; }
.title .url { font-family:"JetBrains Mono", monospace; font-weight:600; font-size:30px; color:var(--blue); margin-top:14px; }
.title .big { font-family:"Instrument Serif", serif; font-size:110px; line-height:1.02; max-width:1500px; }
.title .big em { color:var(--pink); font-style:italic; }
</style>
</head>
<body>
<div id="root" data-composition-id="hyprfm-${cutName}" data-start="0" data-width="${W}" data-height="${H}" data-duration="${TOTAL}">
<div id="glow1" class="glow"></div><div id="glow2" class="glow"></div><div id="grain"></div>
<div id="win"><div id="zoomwrap"><div id="zoom">
${html.filter(h => h.startsWith('<video')).join('\n')}
</div></div></div>
${html.filter(h => !h.startsWith('<video')).join('\n')}
${audio.join('\n')}
</div>
<script>
const tl = gsap.timeline({ paused: true });
${js.join('\n')}
window.__timelines["hyprfm-${cutName}"] = tl;
</script>
</body>
</html>
`;

fs.mkdirSync(path.join(OUT, 'assets/takes'), { recursive: true });
fs.mkdirSync(path.join(OUT, 'assets/sfx'), { recursive: true });
fs.writeFileSync(path.join(OUT, 'index.html'), page);
for (const tk of takesUsed) fs.copyFileSync(path.join(TAKES, `${tk}.mp4`), path.join(OUT, 'assets/takes', `${tk}.mp4`));
for (const f of fs.readdirSync(path.join(HERE, 'sfx'))) fs.copyFileSync(path.join(HERE, 'sfx', f), path.join(OUT, 'assets/sfx', f));
fs.copyFileSync(path.join(HERE, 'logo.svg'), path.join(OUT, 'assets/logo.svg'));
fs.writeFileSync(path.join(OUT, 'timeline.json'), JSON.stringify(scenes.map(s => ({ start: s.start, dur: s.dur, take: s.take ?? 'title', in: s.in })), null, 1));
console.log(`${cutName}: ${scenes.length} scenes, ${TOTAL}s, ${kcN} key overlays, ${aid} sfx`);
