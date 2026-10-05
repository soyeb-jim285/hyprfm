// Edit decision lists for the two cuts. Every cut point is anchored to an
// event in a take's key log (ev) so re-recording a take does not break the
// edit. Times inside a scene (caps, zoom) are seconds from the scene start.
//
// Facts on screen are grounded in the code/docs (see brag-plan.md); the
// ricer, thesis and cats are framing from the demo files.

const OUTRO = {
  html: `<img class="logo" src="assets/logo.svg" alt=""><h1>HyprFM</h1>
<div class="term"><span class="p">$</span> yay -S hyprfm-git</div>
<div class="also">AUR · Flatpak · AppImage · Nix</div>
<div class="url">github.com/soyeb-jim285/hyprfm</div>`,
  anim: sel => [
    `tl.fromTo('${sel} .logo', {scale:0.6, opacity:0}, {scale:1, opacity:1, duration:0.6, ease:'back.out(1.8)'}, @0.25);`,
    `tl.fromTo('${sel} h1', {y:30, opacity:0}, {y:0, opacity:1, duration:0.55, ease:'power3.out'}, @0.4);`,
    `tl.fromTo('${sel} .term', {y:24, opacity:0}, {y:0, opacity:1, duration:0.5, ease:'power3.out'}, @0.9);`,
    `tl.fromTo('${sel} .also', {y:18, opacity:0}, {y:0, opacity:1, duration:0.45, ease:'power2.out'}, @1.5);`,
    `tl.fromTo('${sel} .url', {y:18, opacity:0}, {y:0, opacity:1, duration:0.45, ease:'power2.out'}, @1.7);`,
  ],
  sfx: [{ file: 'impactBell_heavy_000.ogg', t: 0.25, vol: 0.45, d: 2.5 }],
};

export const CUTS = {
  long: {
    scenes: (ev, dur, change) => {
      const s = [];
      // 1 — cold open: the ricer's todo list, slow push into the preview pane
      s.push({ take: 'cold', in: 0.3, dur: 5.6,
        zoom: [{ t: 0, scale: 1.0, ox: 1100, oy: 140, d: 0.01 }, { t: 0.3, scale: 1.75, ox: 1100, oy: 140, d: 5.2, ease: 'sine.inOut' }],
        caps: [
          { t: 0.35, d: 2.45, chapter: 'A field study', text: 'The Hyprland ricer.' },
          { t: 2.9, d: 2.6, text: 'Habitat: <code>~/</code>. Diet: dotfiles.' },
        ] });
      // 2 — launch: cut to the instant before the window appears
      const win = change('launch');
      s.push({ take: 'launch', in: Math.max(0, win - 0.25), dur: 3.8, zoom: [{ t: 0, scale: 1, ox: 800, oy: 500, d: 0.01 }],
        caps: [{ t: 0.35, d: 3.35, chapter: 'HyprFM', text: 'Its file manager opens in <em>163 ms</em>.' }] });
      // 3 — Miller: arrow through the wallpapers, the preview follows
      const wIn = ev('wallpapers', 'type', 'wal') + 0.5;
      s.push({ take: 'wallpapers', in: wIn, dur: Math.min(6.8, dur('wallpapers') - wIn - 0.2),
        caps: [{ t: 0.3, d: 6.4, chapter: 'Miller columns', text: 'The preview follows your arrow keys.' }] });
      // 4 — everything previews inline: jump cuts on the real key presses
      const tour = [
        ['type', 'main', 'Rust, highlighted by bat.', 'Every file previews itself'],
        ['type', 'todo', 'Markdown, rendered. The checklist is honest.'],
        ['type', 'thesis-f', 'PDFs, inline. Page one exists.'],
        ['type', 'dot', 'Archives, listed without unpacking.'],
        ['type', 'fon', 'Fonts, with a specimen.'],
      ];
      for (const [kind, what, text, chapter] of tour) {
        const tin = ev('tour', kind, what) - 0.35;
        const d = what === 'fon' ? 3.0 : 2.6;
        s.push({ take: 'tour', in: tin, dur: Math.min(d, dur('tour') - tin - 0.1),
          caps: [{ t: 0.05, d: d - 0.1, chapter, text }] });
      }
      // 5 — Space quick preview over the cats
      const pIn = ev('peek', 'type', 'pair') - 0.2;
      const pAt = (kind, what, n = 0) => ev('peek', kind, what, n) - pIn;
      const pEnd = pAt('key', 'Right', 1) + 1.5;
      s.push({ take: 'peek', in: pIn, dur: pEnd,
        zoom: [{ t: pAt('key', 'space') + 0.1, scale: 1.18, ox: 800, oy: 520, d: 0.6, ease: 'power3.out' }],
        caps: [
          { t: 0.2, d: pAt('key', 'Right') - 0.3, chapter: 'Quick preview', text: '<code>Space</code> peeks at anything.' },
          { t: pAt('key', 'Right') + 0.1, d: pEnd - pAt('key', 'Right') - 0.2, text: 'Camera EXIF included. The cat did not consent.' },
        ] });
      // 6 — music tags, a video, PDF pages: three jump cuts
      const m1 = ev('peek_media', 'type', 'lof') - 0.2;
      s.push({ take: 'peek_media', in: m1, dur: ev('peek_media', 'key', 'space', 0) + 2.2 - m1, zoom: [{ t: 0.6, scale: 1.18, ox: 800, oy: 520, d: 0.6, ease: 'power3.out' }],
        caps: [{ t: 0.1, d: ev('peek_media', 'key', 'space', 0) + 2.0 - m1, text: 'Music tags. Album: <em>Music To Rice To</em>.' }] });
      const m2 = ev('peek_media', 'key', 'space', 1) - 0.3;
      s.push({ take: 'peek_media', in: m2, dur: 2.9, zoom: [{ t: 0, scale: 1.18, ox: 800, oy: 520, d: 0.01 }],
        caps: [{ t: 0.05, d: 2.8, text: 'Video, with codec and resolution.' }] });
      const m3 = ev('peek_media', 'key', 'space', 2) - 0.3;
      const m3d = ev('peek_media', 'key', 'Escape', 2) - m3;
      s.push({ take: 'peek_media', in: m3, dur: m3d, zoom: [{ t: 0, scale: 1.18, ox: 800, oy: 520, d: 0.01 }],
        caps: [{ t: 0.05, d: m3d - 0.1, text: 'PDF pages, one key at a time.' }] });
      // 7 — split view, copy across, no mouse
      const sIn = ev('split', 'key', 'F3') - 0.3;
      const sD = ev('split', 'chord', 'ctrl+v') + 1.8 - sIn;
      s.push({ take: 'split', in: sIn, dur: sD,
        caps: [{ t: 0.2, d: sD - 0.3, chapter: 'Two panes', text: '<code>F3</code> splits. Copy across without the mouse.' }] });
      // 8 — path bar
      const pbIn = Math.max(0, ev('pathbar', 'chord', 'ctrl+l') - 0.3);
      s.push({ take: 'pathbar', in: pbIn, dur: Math.min(4.4, dur('pathbar') - pbIn - 0.2),
        caps: [{ t: 0.15, d: 4.1, chapter: 'Path bar', text: '<code>Ctrl+L</code>, with suggestions.' }] });
      // 9 — bulk rename: the preview updates while typing
      const rIn = ev('rename', 'key', 'F2') - 0.3;
      const rD = ev('rename', 'key', 'Escape') + 0.1 - rIn;
      s.push({ take: 'rename', in: rIn, dur: rD, zoom: [{ t: 0.3, scale: 1.45, ox: 800, oy: 500, d: 0.6, ease: 'power3.out' }],
        caps: [{ t: 0.15, d: rD - 0.25, chapter: 'Bulk rename', text: 'Rename in bulk. The preview updates as you type.' }] });
      // 10 — themes reload live
      s.push({ take: 'themes', in: 0.6, dur: Math.min(6.6, dur('themes') - 0.8),
        caps: [{ t: 0.2, d: 6.3, chapter: 'Themes', text: '10 themes. Save <code>config.toml</code>, it reloads.' }] });
      // 11 — undo: the thesis goes, the thesis comes back
      const uIn = ev('undo', 'type', 'thesis-f') - 0.3;
      const uAt = (kind, what) => ev('undo', kind, what) - uIn;
      const uD = uAt('chord', 'ctrl+z') + 2.0;
      s.push({ take: 'undo', in: uIn, dur: uD,
        caps: [
          { t: uAt('key', 'Delete') - 0.1, d: uAt('chord', 'ctrl+z') - uAt('key', 'Delete') - 0.1, chapter: 'Undo', text: 'Deleted the thesis?' },
          { t: uAt('chord', 'ctrl+z') + 0.05, d: uD - uAt('chord', 'ctrl+z') - 0.15, text: '<code>Ctrl+Z</code>. It is back.' },
        ] });
      // 12 — punchline
      s.push({ dur: 3.6, html: `<div class="big"><span class="l">HyprFM can restore your thesis.</span><span class="l"><em>It can't write it.</em></span></div>`,
        anim: sel => [`tl.fromTo('${sel} .big', {y:30, opacity:0}, {y:0, opacity:1, duration:0.6, ease:'power3.out'}, @0.2);`],
        sfx: [{ file: 'impactSoft_medium_001.ogg', t: 0.2, vol: 0.5, d: 0.6 }] });
      // 13 — outro
      s.push({ dur: 6.0, ...OUTRO });
      return s;
    },
  },

  short: {
    capSize: 58,
    scenes: (ev, dur, change) => {
      const s = [];
      const win = change('launch');
      s.push({ take: 'launch', in: Math.max(0, win - 0.4), dur: 3.4,
        caps: [{ t: 0.15, d: 1.55, text: 'You riced everything.' }, { t: 1.8, d: 1.5, text: '<em>Except this.</em>' }] });
      s.push({ dur: 3.4, html: `<img class="logo" src="assets/logo.svg" alt=""><h1>HyprFM</h1><div class="sub">A fast, <em>keyboard-friendly</em> file manager for Hyprland.</div>`,
        anim: sel => [
          `tl.fromTo('${sel} .logo', {scale:0.6, opacity:0}, {scale:1, opacity:1, duration:0.5, ease:'back.out(1.8)'}, @0.1);`,
          `tl.fromTo('${sel} h1', {y:26, opacity:0}, {y:0, opacity:1, duration:0.5, ease:'power3.out'}, @0.25);`,
          `tl.fromTo('${sel} .sub', {y:20, opacity:0}, {y:0, opacity:1, duration:0.5, ease:'power3.out'}, @0.5);`,
        ],
        sfx: [{ file: 'impactSoft_medium_001.ogg', t: 0.1, vol: 0.5, d: 0.6 }] });
      const wIn = ev('wallpapers', 'type', 'wal') + 0.5;
      s.push({ take: 'wallpapers', in: wIn, dur: 5.2,
        caps: [{ t: 0.2, d: 4.9, text: 'Every file previews itself.' }] });
      const pIn = ev('peek', 'key', 'space') - 0.4;
      s.push({ take: 'peek', in: pIn, dur: 5.0,
        zoom: [{ t: 0.5, scale: 1.18, ox: 800, oy: 520, d: 0.6, ease: 'power3.out' }],
        caps: [{ t: 0.2, d: 4.7, text: '<code>Space</code> peeks at anything.' }] });
      s.push({ take: 'themes', in: 0.6, dur: 4.4,
        caps: [{ t: 0.2, d: 4.1, text: '10 themes. Live reload.' }] });
      s.push({ dur: 4.4, ...OUTRO });
      return s;
    },
  },
};
