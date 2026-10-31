// The hero background: Miller columns of real file names drifting behind the headline.
// Pauses off screen and in hidden tabs; one still frame under prefers-reduced-motion.
const canvas = document.querySelector('.hero__canvas');

if (canvas) {
  const ctx = canvas.getContext('2d');
  const css = getComputedStyle(document.documentElement);
  const token = (name) => css.getPropertyValue(name).trim();
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let w = 0, h = 0, dpr = 1;
  const scene = columns({ ctx, token });

  const resize = () => {
    const r = canvas.getBoundingClientRect();
    dpr = Math.min(devicePixelRatio || 1, 2);
    w = r.width; h = r.height;
    canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    scene.resize?.(w, h);
    if (still) scene.draw(0, w, h);
  };
  new ResizeObserver(resize).observe(canvas);
  resize();

  if (!still) {
    let visible = true, raf = 0, t0 = performance.now(), paused = 0, last = t0;
    const loop = (now) => { last = now; scene.draw((now - t0 - paused) / 1000, w, h); raf = requestAnimationFrame(loop); };
    const run = () => { if (!raf && visible && !document.hidden) { paused += performance.now() - last; raf = requestAnimationFrame(loop); } };
    const stop = () => { cancelAnimationFrame(raf); raf = 0; };
    new IntersectionObserver(([e]) => { visible = e.isIntersecting; visible ? run() : stop(); }).observe(canvas);
    document.addEventListener('visibilitychange', () => (document.hidden ? stop() : run()));
    run();
  }
}

// Each column drifts at its own speed, one selected row per column stepping down like the arrow key is held.
function columns({ ctx, token }) {
  const names = ['Desktop', 'Documents', 'Downloads', 'Music', 'Pictures', 'Projects', 'Videos', '.config',
    'beskydy-october.jpg', 'conwy-estuary-dusk.jpg', 'esztergom-blue-hour.jpg', 'herbert-lake-mist.jpg',
    'howth-head-gorse.jpg', 'mylgval-cirque.jpg', 'namib-dunes.jpg', 'oeschinensee-perseids.jpg', 'Trips',
    'Wallpapers', 'README.md', 'notes', 'packing.toml', 'route.py', 'cli.py', 'log.txt', 'trailhead', 'dotfiles',
    'hyprland.conf', 'requirements.txt', 'elevation.md', 'beskydy-trail-map.pdf', 'trip-photos.tar.gz',
    'notes-backup.zip', 'Field recordings', 'dawn chorus.ogg', 'harbour wind.ogg', 'rain on tin.ogg',
    'conwy-timelapse.mp4', 'namib-drone.mp4', 'config.toml', 'session.json', 'themes', 'rose-pine-dawn.toml',
    'catppuccin-mocha.toml', 'nord.toml', '.gitignore', 'src', 'docs', 'build', 'CMakeLists.txt', 'main.cpp'];
  const ink = token('--color-muted'), hi = token('--color-ink'), sel = token('--color-accent-deep'), rule = token('--color-rule');
  const ROW = 30, COL = 232;
  let cols = [];
  const seed = (i) => { const x = Math.sin(i * 127.1) * 43758.5453; return x - Math.floor(x); };
  return {
    resize(w, h) {
      const n = Math.ceil(w / COL) + 1;
      cols = Array.from({ length: n }, (_, i) => ({
        speed: (6 + seed(i) * 14) * (i % 2 ? 1 : -1),
        start: Math.floor(seed(i + 9) * names.length),
        len: 14 + Math.floor(seed(i + 3) * 18),
        step: 0.9 + seed(i + 5) * 1.4,
      }));
    },
    draw(t, w, h) {
      ctx.clearRect(0, 0, w, h);
      ctx.font = '500 13px "Wix Madefor Text", system-ui, sans-serif';
      ctx.textBaseline = 'middle';
      cols.forEach((c, i) => {
        const x = i * COL - (w % COL) / 2;
        ctx.globalAlpha = 0.5; ctx.fillStyle = rule; ctx.fillRect(x + COL - 1, 0, 1, h);
        const total = c.len * ROW, off = ((t * c.speed) % total + total) % total;
        const selected = Math.floor(t / c.step) % c.len;
        for (let k = -1; k * ROW - off < h + ROW; k++) {
          const row = ((k % c.len) + c.len) % c.len, y = k * ROW + off - total;
          for (let rep = 0; rep < 2; rep++) {
            const yy = y + rep * total;
            if (yy < -ROW || yy > h) continue;
            const isSel = row === selected;
            if (isSel) { ctx.globalAlpha = 0.55; ctx.fillStyle = sel; ctx.beginPath(); ctx.roundRect(x + 8, yy + 3, COL - 24, ROW - 6, 6); ctx.fill(); }
            ctx.globalAlpha = isSel ? 0.9 : 0.34; ctx.fillStyle = isSel ? hi : ink;
            ctx.fillText(names[(c.start + row * 7) % names.length], x + 20, yy + ROW / 2);
          }
        }
      });
      ctx.globalAlpha = 1;
    },
  };
}
