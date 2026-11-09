const { app, BrowserWindow, nativeTheme } = require('electron');
const fs = require('fs'), path = require('path');
const output = path.resolve(process.argv.find(a => a.startsWith('--fixture-output='))?.split('=').slice(1).join('=') || path.join(__dirname, 'results'));
fs.mkdirSync(output, { recursive: true });
app.setPath('userData', path.join(output, 'user-data'));
app.setPath('sessionData', path.join(output, 'session-data'));
const events = []; let win, busy = false, lastCommand = '', rebuilds = 0;
const theme = process.argv.includes('--fixture-light') ? 'light' : 'dark';
nativeTheme.themeSource = theme;
app.whenReady().then(async () => {
  win = new BrowserWindow({ width: 1100, height: 700, title: 'WindowStyler owned Electron fixture', titleBarStyle: 'hidden', titleBarOverlay: { color: theme === 'dark' ? '#202124' : '#f0f0f0', symbolColor: theme === 'dark' ? '#ffffff' : '#202124', height: 36 }, webPreferences: { sandbox: true } });
  for (const name of ['maximize', 'unmaximize', 'minimize', 'restore', 'close']) win.on(name, () => { events.push(name); fs.writeFileSync(path.join(output, 'events.json'), JSON.stringify(events)); });
  await win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(`<!doctype html><html><head><style>
  :root {color-scheme:${theme}} *{box-sizing:border-box} body{margin:0;background:${theme === 'dark' ? '#202124' : '#f0f0f0'};color:${theme === 'dark' ? '#ffffff' : '#202124'};font:14px system-ui} header{position:absolute;left:0;top:0;width:100%;height:36px;-webkit-app-region:drag;display:flex;align-items:center} .left{position:absolute;left:0;display:flex;align-items:center;height:100%} .center{position:absolute;left:50%;transform:translateX(-50%)} .right{position:absolute;right:140px;display:flex;gap:8px} button,input{-webkit-app-region:no-drag} main{position:absolute;top:36px;left:0;right:0;bottom:0;padding:24px} input{width:230px}
  </style></head><body><header><div class="left"><button aria-label="Application menu">Menu</button><button>File</button><button>Edit</button></div><div class="center"><input aria-label="Search" value="Toolbar input remains usable"></div><div class="right"><button>Layout</button><button>Settings</button></div></header><main><h1>Owned Electron runtime fixture</h1><p>Absolute title row children, original native overlay, isolated data directory.</p><textarea aria-label="Editor" style="width:90%;height:250px">Original content</textarea></main></body></html>`));
  win.show();
  await win.webContents.executeJavaScript("window.__fixtureOriginalHeader=document.querySelector('header').outerHTML");
  const hwnd = Number(win.getNativeWindowHandle().readBigUInt64LE());
  fs.writeFileSync(path.join(output, 'ready.json'), JSON.stringify({ pid: process.pid, hwnd: '0x' + hwnd.toString(16), versions: process.versions, theme }));
  setInterval(async () => {
    if (busy || win.isDestroyed()) return; busy = true;
    try {
      const commandPath = path.join(output, 'command.json');
      if (fs.existsSync(commandPath)) {
        const raw = fs.readFileSync(commandPath, 'utf8');
        if (raw !== lastCommand) {
          lastCommand = raw; const c = JSON.parse(raw);
          if (c.action === 'close') { win.close(); return; }
          if (c.action === 'click') await win.webContents.executeJavaScript(`[...document.querySelectorAll('#macdows-lights-v7 button')].find(e=>e.getAttribute('aria-label')===${JSON.stringify(c.label)}).click()`);
          if (c.action === 'theme') {nativeTheme.themeSource = c.value; win.setTitleBarOverlay({color:c.value==='dark'?'#202124':'#f0f0f0',symbolColor:c.value==='dark'?'#ffffff':'#202124'}); await win.webContents.executeJavaScript(`document.body.style.background=${JSON.stringify(c.value==='dark'?'#202124':'#f0f0f0')};document.body.style.color=${JSON.stringify(c.value==='dark'?'#ffffff':'#202124')}`);}
          if (c.action === 'resize') win.setSize(c.width,c.height);
          if (c.action === 'capabilities') {win.setClosable(c.close);win.setMinimizable(c.minimize);win.setMaximizable(c.maximize);}
          if (c.action === 'reload') win.reload();
          if (c.action === 'rebuild') {await win.webContents.executeJavaScript("document.querySelector('header').outerHTML=window.__fixtureOriginalHeader");rebuilds++;}
          if (c.action === 'restore') win.restore();
        }
      }
      const layout = await win.webContents.executeJavaScript(`({lights:!!document.querySelector('#macdows-lights-v7'),lightsState:[...document.querySelectorAll('#macdows-lights-v7 button')].map(e=>({disabled:e.disabled,color:getComputedStyle(e.firstElementChild).backgroundColor})),items:[...document.querySelectorAll('header button,header input')].map(e=>({label:e.getAttribute('aria-label')||e.textContent,rect:[e.getBoundingClientRect().x,e.getBoundingClientRect().y,e.getBoundingClientRect().width,e.getBoundingClientRect().height]})),text:document.querySelector('textarea').value})`);
      fs.writeFileSync(path.join(output, 'status.json'), JSON.stringify({ events, layout, rebuilds, maximized:win.isMaximized(), minimized:win.isMinimized() }));
    } catch (e) {fs.writeFileSync(path.join(output,'fixture-error.txt'),String(e.stack));}
    finally {busy=false;}
  }, 150);
});
app.on('window-all-closed', () => app.quit());
