// Theme picker for the HyprFM window copy: a radio group with arrow keys. The colours ease
// across through registered custom properties (site.css), like Theme.qml's ColorAnimations.
const picks = document.querySelector('[data-fm-picks]');
const fm = document.querySelector('.fm');

if (picks && fm) {
  const radios = [...picks.querySelectorAll('[role=radio]')];
  const pick = (r, focus) => {
    for (const o of radios) { o.setAttribute('aria-checked', String(o === r)); o.tabIndex = o === r ? 0 : -1; }
    fm.dataset.fm = r.dataset.fm;
    if (focus) r.focus();
  };
  picks.addEventListener('click', (e) => { const r = e.target.closest('[role=radio]'); if (r) pick(r); });
  picks.addEventListener('keydown', (e) => {
    const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key];
    if (!step) return;
    e.preventDefault();
    const i = radios.indexOf(document.activeElement);
    pick(radios[(i + step + radios.length) % radios.length], true);
  });
}
