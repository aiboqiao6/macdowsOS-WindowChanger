function(action, hwnd, addonFile) {
  try {
    const req = process.mainModule?.require.bind(process.mainModule);
    if (!req) return 'unsupported';
    const electron = req('electron');
    const window = electron.BrowserWindow.getAllWindows().find(w => Number(w.getNativeWindowHandle().readBigUInt64LE()) === hwnd);
    if (!window || window.isDestroyed() || window.webContents.isDestroyed()) return 'unsupported';
    if (action === 'probe') return 'supported';
    const key = '__macdowsGeneric:' + addonFile;
    const state = globalThis[key] ||= { addon: null, windows: new Map() };
    if (!state.addon) { const module = { exports: {} }; process.dlopen(module, addonFile); state.addon = module.exports; }
    const signal = (...args) => state.addon.signal(...args);

    // Shared renderer discovery: semantic window commands, app-region geometry
    // and the public title overlay API. No executable or CSS class selectors.
    function renderer(mode, key, token) {
      if (mode === 'detach') { globalThis[key]?.restore(); delete globalThis[key]; return true; }
      if (globalThis[key]) throw Error('Renderer lease already exists');
      const attributes = [], changes = [], added = [];
      const restore = () => {
        for (const e of added) e.remove();
        for (const [e, name, value] of attributes.reverse()) value === null ? e.removeAttribute(name) : e.setAttribute(name, value);
        for (const undo of changes.reverse()) undo();
      };
      const mark = (e, name, value) => { attributes.push([e, name, e.getAttribute(name)]); e.setAttribute(name, value); };
      const rect = e => e.getBoundingClientRect();
      const visible = e => { const r = rect(e), s = getComputedStyle(e); return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && s.display !== 'none'; };
      const command = e => {
        const label = (e.getAttribute('aria-label') || e.getAttribute('title') || e.getAttribute('data-action') || '').trim();
        if (/^(?:close(?: window)?|关闭(?:窗口)?)$/i.test(label)) return 'close';
        if (/^(?:minimize(?: window)?|最小化(?:窗口)?)$/i.test(label)) return 'minimize';
        if (/^(?:(?:maximize|restore)(?: window)?|(?:撤销)?最大化(?:窗口)?|还原(?:窗口)?)$/i.test(label)) return 'maximize';
        return null;
      };
      try {
        const all = [...document.querySelectorAll('body *')];
        const candidates = all.filter(e => command(e) && visible(e) && rect(e).top >= -1 && rect(e).top < 48 && rect(e).height <= 48 && rect(e).width <= 100);
        const cluster = candidates => {
          const groups = new Map();
          for (const e of candidates) { const list = groups.get(e.parentElement) || []; list.push(e); groups.set(e.parentElement, list); }
          const valid = [...groups.values()].filter(list => list.length === 3 && ['close', 'minimize', 'maximize'].every(a => list.filter(e => command(e) === a).length === 1));
          if (valid.length > 1) throw Error('Ambiguous semantic command clusters');
          return valid[0];
        };
        let controls = cluster(candidates);
        const overlay = !!navigator.windowControlsOverlay?.visible;
        let css = '', height, lights;
        if (controls) {
          height = Math.max(...controls.map(e => rect(e).height));
          if (height < 20 || height > 48) throw Error('Unsupported command row geometry');
          for (const e of controls) mark(e, 'data-macdows-command', command(e));
          css = '[data-macdows-command]{position:fixed!important;top:0!important;right:auto!important;width:20px!important;height:' + height + 'px!important;min-width:20px!important;background:transparent!important;-webkit-app-region:no-drag!important;z-index:2147483000!important;}' +
            '[data-macdows-command="close"]{left:8px!important;}[data-macdows-command="minimize"]{left:28px!important;}[data-macdows-command="maximize"]{left:48px!important;}' +
            '[data-macdows-command]>*{visibility:hidden!important;}[data-macdows-command]::after{content:""!important;position:absolute!important;width:12px!important;height:12px!important;left:4px!important;top:' + ((height - 12) / 2) + 'px!important;border-radius:50%!important;pointer-events:none!important;}' +
            '[data-macdows-command="close"]::after{background:#ff5f57!important;}[data-macdows-command="minimize"]::after{background:#ffbd2e!important;}[data-macdows-command="maximize"]::after{background:#28c840!important;}';
          const obstacles = all.filter(e => {
            if (!visible(e) || e.matches('button,input,[role="button"]') || controls.some(b => b.contains(e) || e.contains(b))) return false;
            const r = rect(e); return r.left >= 0 && r.left < 4 && r.right <= 84 && r.width >= 12 && r.top >= 0 && r.top < height && r.height <= 32;
          });
          for (const e of obstacles.filter(e => !obstacles.some(parent => parent !== e && parent.contains(e)))) {
            const r = rect(e), dy = height + 8 - r.top;
            if (all.some(other => other !== e && !e.contains(other) && other.matches('button,input,[role="button"]') && visible(other) && rect(other).left < r.right && rect(other).right > r.left && rect(other).top >= r.bottom && rect(other).top < r.bottom + dy)) throw Error('Corner reserve would overlap an existing control');
            const value = e.style.getPropertyValue('transform'), priority = e.style.getPropertyPriority('transform'), original = getComputedStyle(e).transform;
            changes.push(() => value ? e.style.setProperty('transform', value, priority) : e.style.removeProperty('transform'));
            e.style.setProperty('transform', (original === 'none' ? '' : original + ' ') + 'translateY(' + dy + 'px)', 'important');
          }
        } else if (overlay) {
          const bars = all.filter(e => { const r = rect(e); return visible(e) && r.top >= -1 && r.top <= 1 && r.left <= 1 && r.width >= innerWidth * .8 && r.height >= 24 && r.height <= 64 && getComputedStyle(e).webkitAppRegion === 'drag'; });
          if (bars.length !== 1) throw Error('Cannot identify a unique original overlay row');
          const bar = bars[0]; height = rect(bar).height;
          mark(bar, 'data-macdows-title-row', '1');
          css = '[data-macdows-title-row]{position:relative!important;padding-left:' + (parseFloat(getComputedStyle(bar).paddingLeft) + 84) + 'px!important;box-sizing:border-box!important;}';
          lights = document.createElement('div'); lights.id = 'macdows-lights-v7';
          for (const [action, color, label] of [['close', '#ff5f57', 'Close window'], ['minimize', '#ffbd2e', 'Minimize window'], ['maximize', '#28c840', 'Maximize or restore window']]) {
            const b = document.createElement('button'), dot = document.createElement('span'); b.setAttribute('aria-label', label); dot.style.background = color; b.append(dot); b.addEventListener('click', () => console.info(token + action)); lights.append(b);
          }
          bar.append(lights); added.push(lights);
          css += '#macdows-lights-v7{position:absolute;left:0;top:0;width:84px;height:100%;display:flex;align-items:center;padding-left:8px;box-sizing:border-box;z-index:2147483000;-webkit-app-region:no-drag;}#macdows-lights-v7 button{all:initial;display:flex;width:20px;height:24px;align-items:center;justify-content:center;cursor:default;-webkit-app-region:no-drag;}#macdows-lights-v7 button:focus-visible{outline:1px solid currentColor;border-radius:5px;}#macdows-lights-v7 span{display:block;width:12px;height:12px;border-radius:50%;pointer-events:none;}';
        } else throw Error('No semantic command cluster or title overlay');
        const style = document.createElement('style'); style.textContent = css; document.head.append(style); added.push(style);
        if (controls && controls.some(e => Math.abs(rect(e).left - ({close:8,minimize:28,maximize:48}[command(e)])) > 1)) throw Error('Controls did not reach the reserved corner');
        if (lights && (rect(lights).left > 4 || rect(lights.parentElement.firstElementChild).left < 80)) throw Error('Original row did not reserve space');
        if (controls) {
          // Frameworks can replace the maximize button when it becomes restore.
          // Re-discover the semantic cluster after mutations; never retain a
          // selector tied to a particular application's class or child order.
          const observer = new MutationObserver(() => {
            const next = cluster([...document.querySelectorAll('[aria-label],[title],[data-action]')].filter(e => command(e) && visible(e) && rect(e).top >= -1 && rect(e).top < 48 && rect(e).height <= 48 && rect(e).width <= 100));
            if (next) for (const e of next) if (e.getAttribute('data-macdows-command') !== command(e)) mark(e, 'data-macdows-command', command(e));
          });
          observer.observe(document.body, { subtree: true, childList: true, attributes: true, attributeFilter: ['aria-label', 'title', 'data-action'] });
          changes.push(() => observer.disconnect());
        }
        globalThis[key] = { restore };
        return { overlay: !controls && overlay, height, metrics: [0, 0, 84, height, 84, 0].map(n => Math.round(n * devicePixelRatio)) };
      } catch (error) { restore(); throw error; }
    }
    const execute = (mode, token = '') => window.webContents.executeJavaScript('(' + renderer.toString() + ')(' + JSON.stringify(mode) + ',' + JSON.stringify(key) + ',' + JSON.stringify(token) + ')');
    if (action === 'detach') {
      state.windows.get(hwnd)?.restore();
      execute('detach').then(() => { state.windows.delete(hwnd); signal(2, hwnd); }).catch(() => signal(3, hwnd));
      return 'scheduled';
    }
    if (action !== 'attach') return 'unsupported';
    const token = '__macdows_' + req('crypto').randomBytes(16).toString('hex') + ':';
    const record = { restore() {} }; state.windows.set(hwnd, record);
    execute('attach', token).then(plan => {
      if (plan.overlay) {
        const original = window.setTitleBarOverlay, descriptor = Object.getOwnPropertyDescriptor(window, 'setTitleBarOverlay');
        let settings = { color: '#00000000', symbolColor: electron.nativeTheme.shouldUseDarkColors ? '#ffffff' : '#1f1f1f', height: Math.round(plan.height * window.webContents.getZoomFactor()) };
        const setter = function(options) { settings = { ...settings, ...options }; return original.call(this, { ...options, symbolColor: '#00000000' }); };
        const listener = (...args) => {
          const message = args.map(a => typeof a === 'string' ? a : a && typeof a.message === 'string' ? a.message : '').find(m => m.startsWith(token));
          if (!message || window.isDestroyed()) return;
          switch (message.slice(token.length)) { case 'close': window.close(); break; case 'minimize': window.minimize(); break; case 'maximize': window.isMaximized() ? window.unmaximize() : window.maximize(); break; }
        };
        record.restore = () => { window.webContents.removeListener('console-message', listener); if (descriptor) Object.defineProperty(window, 'setTitleBarOverlay', descriptor); else delete window.setTitleBarOverlay; if (!window.isDestroyed()) original.call(window, settings); };
        window.setTitleBarOverlay = setter; setter.call(window, settings); window.webContents.on('console-message', listener);
      }
      signal(1, hwnd, ...plan.metrics);
    }).catch(error => { record.restore(); execute('detach').finally(() => { state.windows.delete(hwnd); req('fs').writeFileSync(addonFile + '.error.txt', String(error)); signal(3, hwnd); }); });
    return 'scheduled';
  } catch (error) { return 'ERR:' + String(error); }
}
