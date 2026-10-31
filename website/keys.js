// The shortcut keyboard. The shortcuts come from the static list below it (the same 49 defaults
// as ConfigManager::s_defaultShortcuts); this only lays them out on a keyboard, one layer per
// modifier combination, switched by the buttons or by holding the real modifiers.
const board = document.querySelector('[data-board]');

if (board) {
  const grid = board.querySelector('[data-keys-grid]');
  const say = board.querySelector('[data-say]');
  const buttons = [...board.querySelectorAll('[data-layer]')];
  const MODS = ['Ctrl', 'Shift', 'Alt'];

  // [id, label, x, y, width] in key units; the board is 18.5 x 6.25
  const keys = [];
  const row = (y, x, ids, w = 1) => ids.forEach((id, i) => keys.push([id, id, x + i * w, y, w]));
  const key = (id, x, y, w = 1, label = id) => keys.push([id, label, x, y, w]);
  key('Esc', 0, 0);
  row(0, 2, ['F1', 'F2', 'F3', 'F4']); row(0, 6.5, ['F5', 'F6', 'F7', 'F8']); row(0, 11, ['F9', 'F10', 'F11', 'F12']);
  row(1.25, 0, ['`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=']); key('Backspace', 13, 1.25, 2);
  key('Tab', 0, 2.25, 1.5); row(2.25, 1.5, ['Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']']); key('\\', 13.5, 2.25, 1.5);
  key('Caps', 0, 3.25, 1.75); row(3.25, 1.75, ['A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', "'"]); key('Return', 12.75, 3.25, 2.25, 'Enter');
  key('Shift', 0, 4.25, 2.25); row(4.25, 2.25, ['Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/']); key('Shift', 12.25, 4.25, 2.75);
  key('Ctrl', 0, 5.25, 1.25); key('Super', 1.25, 5.25, 1.25); key('Alt', 2.5, 5.25, 1.25); key('Space', 3.75, 5.25, 6.25);
  key('Alt', 10, 5.25, 1.25); key('Super', 11.25, 5.25, 1.25); key('Menu', 12.5, 5.25, 1.25); key('Ctrl', 13.75, 5.25, 1.25);
  row(1.25, 15.5, ['Ins', 'Home', 'PgUp']); key('Delete', 15.5, 2.25, 1, 'Del'); row(2.25, 16.5, ['End', 'PgDn']);
  key('Up', 16.5, 4.25, 1, '↑'); key('Left', 15.5, 5.25, 1, '←'); key('Down', 16.5, 5.25, 1, '↓'); key('Right', 17.5, 5.25, 1, '→');

  // layer "Ctrl+Shift" -> key id -> { label, cat, combo }
  const layers = {};
  const put = (combo, label, cat) => {
    let parts = combo.split('+');
    if (combo === 'Ctrl+?') parts = ['Ctrl', 'Shift', '/']; // ? is Shift+/ on a US layout
    const id = parts.pop() || '+';
    const layer = MODS.filter((m) => parts.includes(m)).join('+');
    (layers[layer] ||= {})[id] = { label, cat, combo };
  };
  for (const item of document.querySelectorAll('.keys__all [data-keys]')) {
    put(item.dataset.keys, item.querySelector('dd').textContent, item.closest('[data-cat]').dataset.cat);
  }
  for (const id of ['Up', 'Down', 'Left', 'Right']) put(id, 'Move', 'go');

  const els = keys.map(([id, label, x, y, w]) => {
    const el = document.createElement('span');
    el.className = MODS.includes(id) ? 'key key--mod' : 'key';
    el.dataset.id = id;
    el.style.cssText = `--x: ${x}; --y: ${y}; --w: ${w}`;
    el.dataset.label = label; // drawn by CSS: a legend, not reading text
    el.append(document.createElement('i'));
    grid.append(el);
    return el;
  });

  let layer = '';
  const setLayer = (next) => {
    layer = next;
    const map = layers[layer] || {};
    for (const el of els) {
      const hit = map[el.dataset.id];
      el.dataset.cat = hit ? hit.cat : '';
      el.lastChild.textContent = hit ? hit.label : '';
      el.classList.toggle('is-held', layer.split('+').includes(el.dataset.id));
    }
    for (const b of buttons) b.setAttribute('aria-pressed', String(b.dataset.layer === layer));
  };
  const tell = (el) => {
    const hit = (layers[layer] || {})[el.dataset.id];
    if (hit) say.textContent = `${hit.combo.replaceAll('+', ' ')}: ${hit.label}`;
  };

  buttons.forEach((b) => b.addEventListener('click', () => setLayer(b.dataset.layer)));
  grid.addEventListener('pointerover', (e) => { const el = e.target.closest('.key'); if (el) tell(el); });

  // the real keyboard drives the board while it is on screen; nothing is prevented
  let inView = false;
  new IntersectionObserver(([e]) => { inView = e.isIntersecting; }, { threshold: 0.4 }).observe(grid);
  const fromEvent = (e) => MODS.filter((m) => e[{ Ctrl: 'ctrlKey', Shift: 'shiftKey', Alt: 'altKey' }[m]]).join('+');
  const named = { Enter: 'Return', ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right', ' ': 'Space', Escape: 'Esc', Delete: 'Delete', '?': '/' };
  addEventListener('keydown', (e) => {
    if (!inView || e.target.closest?.('input, textarea, button, a, summary')) return;
    if (fromEvent(e) !== layer) setLayer(fromEvent(e));
    const id = named[e.key] || (e.key.length === 1 ? e.key.toUpperCase() : e.key);
    const el = els.find((k) => k.dataset.id === id);
    if (!el) return;
    el.classList.add('is-hit'); tell(el);
  });
  addEventListener('keyup', (e) => {
    if (!inView) return;
    els.forEach((k) => k.classList.remove('is-hit'));
    if (fromEvent(e) !== layer) setLayer(fromEvent(e));
  });
  addEventListener('blur', () => setLayer(''));

  setLayer('');
  board.hidden = false;
}
