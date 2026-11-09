function(action, hwnd, addonFile) {
  try {
    // ESM-based main processes do not expose process.mainModule. Use Node's
    // public module API there; the original CommonJS path remains available.
    const req = process.mainModule?.require.bind(process.mainModule) ||
      process.getBuiltinModule?.('module')?.createRequire(process.execPath);
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
    function renderer(mode, key, token, capabilities) {
      if (mode === 'detach') { globalThis[key]?.restore(); delete globalThis[key]; return true; }
      if (mode === 'capabilities') {globalThis[key]?.update(capabilities); return true;}
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
        let css = '', height, lights, overlayRow, checkRow, reserveCenter, watchedRows=[], repairRows=()=>{};
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
          if (bars.length > 1) throw Error('Cannot identify a unique original overlay row');
          let bar = bars[0];
          if (!bar) {
            // Native title-overlay pages can leave their caption entirely empty
            // in the DOM (for example, an onboarding window). The public API
            // identifies that existing area; do not invent an extra title band.
            const area = navigator.windowControlsOverlay.getTitlebarAreaRect();
            if (Math.abs(area.x)>1 || Math.abs(area.y)>1 || area.width<84 || area.height<24 || area.height>64) throw Error('Unsupported native overlay area');
            bar = overlayRow = document.body; height = area.height;
            watchedRows=[bar];
            checkRow = () => {
              const current = navigator.windowControlsOverlay.getTitlebarAreaRect();
              if (!navigator.windowControlsOverlay.visible || Math.abs(current.height-height)>1 || rect(lights).left>1) throw Error('Native overlay area changed');
              const obstacle = [...document.querySelectorAll('body *')].some(e => {
                if (e===lights || lights?.contains(e) || !visible(e)) return false;
                const r=rect(e);
                if (r.top>=height || r.bottom<=0 || r.left>=84 || r.right<=0) return false;
                return e.matches('button,input,select,textarea,a,[role="button"],[role="menuitem"],img,svg,canvas') || !e.children.length && e.textContent.trim().length>0;
              });
              if (obstacle) throw Error('Native overlay corner contains original content');
            };
            reserveCenter = () => {};
          } else {
          const actionable = 'button,input,select,textarea,a,[role="button"],[role="menuitem"],[role="checkbox"]';
          // Some frameworks expose a separate empty drag layer. Its nearest
          // title-sized ancestor owns the actual flex/menu/tool layout.
          while (!bar.querySelector(actionable)) {
            const parent = bar.parentElement, r = parent && rect(parent);
            if (!parent || r.top < -1 || r.top > 1 || r.height < 24 || r.height > 64 || r.width < innerWidth * .8) throw Error('Cannot identify the original overlay content row');
            bar = parent;
          }
          for (let parent=bar.parentElement; parent; parent=parent.parentElement) {
            const r=rect(parent);
            if (r.top < -1 || r.top > 1 || r.height < 24 || r.height > 64 || r.width < innerWidth*.8) break;
            bar=parent;
          }
          overlayRow = bar; height = rect(bar).height;
          const externalRows=[];
          const shiftExternal = () => {
          for (const e of all) {
            if (bar.contains(e) || e.contains(bar) || externalRows.some(row=>row.contains(e)) || !visible(e)) continue;
            const r=rect(e), s=getComputedStyle(e);
            if (!['fixed','absolute'].includes(s.position) || Math.abs(r.top)>1 || r.left<0 || r.left>=84 || r.height<20 || r.height>height+1 || r.width>innerWidth*.8 || !e.textContent.trim() && !e.querySelector(actionable+',img,svg,canvas')) continue;
            if (s.translate!=='none') throw Error('Original overlay lane already translates');
            const value=e.style.getPropertyValue('translate'),priority=e.style.getPropertyPriority('translate');
            changes.push(()=>value?e.style.setProperty('translate',value,priority):e.style.removeProperty('translate'));
            e.style.setProperty('translate','84px 0','important');externalRows.push(e);
          }
          };
          const clipped = e => {
            const r = rect(e); let left = Math.max(0,r.left), right = Math.min(innerWidth,r.right), top = Math.max(0,r.top), bottom = Math.min(innerHeight,r.bottom);
            for (let p = e.parentElement; p; p = p.parentElement) {
              const s = getComputedStyle(p), b = rect(p);
              if (s.overflowX !== 'visible') {left = Math.max(left, b.left); right = Math.min(right, b.right);}
              if (s.overflowY !== 'visible') {top = Math.max(top, b.top); bottom = Math.min(bottom, b.bottom);}
              if (p === bar) break;
            }
            return {left, right, top, bottom, width: Math.max(0, right-left), height: Math.max(0, bottom-top)};
          };
          const items = () => [...document.querySelectorAll('body *')].filter(e => {
            if(lights?.contains(e) || !visible(e))return false;
            const r=rect(e);if(r.top>=height || r.bottom<=0)return false;
            if(e.matches(actionable+',img,svg,canvas'))return true;
            if(e.children.length)return false;
            const s=getComputedStyle(e), pseudo=getComputedStyle(e,'::before').content;
            return e.textContent.trim().length>0 || s.backgroundImage!=='none' || !['none','normal','""'].includes(pseudo);
          }).map(e => [e, clipped(e)]).filter(([,r]) => r.width > 1 && r.height > 1 && r.top < height && r.bottom > 0);
          const before = new Map(items());
          shiftExternal();
          watchedRows=[bar,...externalRows];
          repairRows=()=>{for(const row of externalRows){
            if(!row.isConnected)throw Error('Original overlay lane was replaced');
            const s=getComputedStyle(row),r=rect(row);
            if(!row.style.getPropertyValue('translate') && s.translate==='none'){
              if(Math.abs(r.top)>1 || r.height<20 || r.height>height+1 || r.left<0 || r.left>=84 || r.width>innerWidth*.8)throw Error('Original overlay lane geometry changed');
              row.style.setProperty('translate','84px 0','important');
            }
          }};
          const originalCenter = (rect(bar).left + rect(bar).right) / 2;
          const area = (a,b) => Math.max(0, Math.min(a.right,b.right)-Math.max(a.left,b.left)) * Math.max(0, Math.min(a.bottom,b.bottom)-Math.max(a.top,b.top));
          checkRow = () => {
            if (!bar.isConnected || Math.abs(rect(bar).top) > 1 || Math.abs(rect(bar).height-height) > 1 || rect(lights).left > 1) throw Error('Original overlay row changed');
            const current = items();
            if (!current.length || current.some(([,r]) => r.left < 80 && r.right > 0)) throw Error('Original row did not reserve space');
            const currentMap=new Map(current);
            for(const [e,r] of before) {
              const now=currentMap.get(e);
              if(!now || now.width<Math.min(r.width,84)-2 || now.height<r.height-2) throw Error('Overlay reserve clips an original control');
            }
            for (let i=0; i<current.length; i++) for (let j=i+1; j<current.length; j++) {
              const [a,ra]=current[i], [b,rb]=current[j];
              if (a.contains(b) || b.contains(a)) continue;
              const old = before.has(a) && before.has(b) ? area(before.get(a),before.get(b)) : 0;
              if (area(ra,rb) > old + 2) throw Error('Overlay reserve overlaps original controls');
            }
          };
          reserveCenter = () => {
            // A fixed-width centered field gives up the reserved width when
            // the side groups would otherwise overlap it. Preserve its height
            // and validate all controls again before accepting the transaction.
            const centeredCandidates = [...before].filter(([,r]) => r.width >= 168 && Math.abs((r.left+r.right)/2-originalCenter) <= 2);
            const centered = centeredCandidates.filter(([e])=>!centeredCandidates.some(([parent])=>parent!==e&&parent.contains(e)));
            const current = items();
            const overlap = current.some(([a,ra],i) => current.slice(i+1).some(([b,rb]) => !a.contains(b) && !b.contains(a) && area(ra,rb) > (before.has(a)&&before.has(b)?area(before.get(a),before.get(b)):0)+2));
            if (overlap && centered.length === 1) {
              const [e,r] = centered[0];
              for (const [name,value] of [['box-sizing','border-box'],['min-width','0'],['max-width',(r.width-84)+'px'],['width',(r.width-84)+'px']]) {
                const saved=e.style.getPropertyValue(name), priority=e.style.getPropertyPriority(name);
                changes.push(() => saved ? e.style.setProperty(name,saved,priority) : e.style.removeProperty(name));
                e.style.setProperty(name,value,'important');
              }
              if (Math.abs(rect(e).height-r.height)>1) throw Error('Centered overlay content changed row height');
            }
          };
          mark(bar, 'data-macdows-title-row', '1');
          const barStyle=getComputedStyle(bar);
          css = '[data-macdows-title-row]{position:'+(barStyle.position==='static'?'relative':barStyle.position)+'!important;padding-left:' + (parseFloat(barStyle.paddingLeft) + 84) + 'px!important;box-sizing:border-box!important;}';
          css += '[data-macdows-title-content]{max-width:100%!important;min-width:0!important;box-sizing:border-box!important;}';
          for (const child of bar.children) {
            const s = getComputedStyle(child), r = rect(child);
            if(!['absolute','fixed'].includes(s.position) && r.width>=rect(bar).width*.8)mark(child,'data-macdows-title-content','1');
            const content=child.matches(actionable+',img,svg,canvas') || child.querySelector(actionable+',img,svg,canvas') || child.textContent.trim() || s.backgroundImage!=='none';
            if (!['absolute','fixed'].includes(s.position) || r.left >= 84 || r.right <= 0 || !content) continue;
            if (r.width > rect(bar).width - 84) throw Error('Absolute overlay content cannot reserve the corner');
            const value = child.style.getPropertyValue('translate'), priority = child.style.getPropertyPriority('translate');
            changes.push(() => value ? child.style.setProperty('translate', value, priority) : child.style.removeProperty('translate'));
            if (s.translate !== 'none') throw Error('Original overlay content already translates');
            child.style.setProperty('translate', '84px 0', 'important');
          }
          }
          lights = document.createElement('div'); lights.id = 'macdows-lights-v7';
          for (const [action, color, label] of [['close', '#ff5f57', 'Close window'], ['minimize', '#ffbd2e', 'Minimize window'], ['maximize', '#28c840', 'Maximize or restore window']]) {
            const b = document.createElement('button'), dot = document.createElement('span'); b.setAttribute('aria-label', label); b.disabled = !capabilities[['close','minimize','maximize'].indexOf(action)]; dot.style.background = b.disabled ? '#808080' : color; b.append(dot); b.addEventListener('click', () => console.info(token + action)); lights.append(b);
          }
          bar.append(lights); added.push(lights);
          css += '#macdows-lights-v7{position:'+(bar===document.body?'fixed':'absolute')+';left:0;top:0;width:84px;height:'+height+'px;display:flex;align-items:center;padding-left:8px;box-sizing:border-box;z-index:2147483000;-webkit-app-region:no-drag;}#macdows-lights-v7 button{all:initial;display:flex;width:20px;height:24px;align-items:center;justify-content:center;cursor:default;-webkit-app-region:no-drag;}#macdows-lights-v7 button:focus-visible{outline:1px solid currentColor;border-radius:5px;}#macdows-lights-v7 span{display:block;width:12px;height:12px;border-radius:50%;pointer-events:none;}';
        } else throw Error('No semantic command cluster or title overlay');
        const style = document.createElement('style'); style.textContent = css; document.head.append(style); added.push(style);
        if (controls && controls.some(e => Math.abs(rect(e).left - ({close:8,minimize:28,maximize:48}[command(e)])) > 1)) throw Error('Controls did not reach the reserved corner');
        if (lights) {
          reserveCenter();
          checkRow();
          let stopped = false;
          const validate = () => {
            if (stopped) return;
            try {repairRows();checkRow();} catch (_) {stopped=true; restore(); delete globalThis[key]; console.info(token + 'invalid');}
          };
          const observer = new MutationObserver(records=>{if(records.some(record=>watchedRows.some(row=>row.contains(record.target)||record.target.contains(row))))validate();}), resize = new ResizeObserver(validate);
          observer.observe(document.body, {subtree:true,childList:true,attributes:true,attributeFilter:['style','class','hidden','aria-hidden']});
          for(const row of watchedRows)resize.observe(row);
          changes.push(() => {stopped=true; observer.disconnect(); resize.disconnect();});
        }
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
        const update = values => {
          if (!lights) return;
          [...lights.children].forEach((button,i) => {button.disabled=!values[i];button.firstElementChild.style.background=values[i]?['#ff5f57','#ffbd2e','#28c840'][i]:'#808080';});
        };
        globalThis[key] = { restore, update };
        return { overlay: !controls && overlay, height, metrics: [0, 0, 84, height, 84, 0].map(n => Math.round(n * devicePixelRatio)) };
      } catch (error) { restore(); return {error:String(error)}; }
    }
    const execute = (mode, token = '') => window.webContents.executeJavaScript('(' + renderer.toString() + ')(' + JSON.stringify(mode) + ',' + JSON.stringify(key) + ',' + JSON.stringify(token) + ',' + JSON.stringify([window.isClosable(),window.isMinimizable(),window.isMaximizable()]) + ')');
    if (action === 'detach') {
      state.windows.get(hwnd)?.restore();
      execute('detach').then(() => { state.windows.delete(hwnd); signal(2, hwnd); }).catch(() => signal(3, hwnd));
      return 'scheduled';
    }
    if (action !== 'attach') return 'unsupported';
    const token = '__macdows_' + req('crypto').randomBytes(16).toString('hex') + ':';
    const record = { restore() {} }; state.windows.set(hwnd, record);
    execute('attach', token).then(plan => {
      if (plan.error) throw Error(plan.error);
      if (plan.overlay) {
        const original = window.setTitleBarOverlay, descriptor = Object.getOwnPropertyDescriptor(window, 'setTitleBarOverlay');
        let settings = { symbolColor: electron.nativeTheme.shouldUseDarkColors ? '#ffffff' : '#1f1f1f' };
        const setter = function(options) { settings = { ...settings, ...options }; return original.call(this, { ...options, symbolColor: '#00000000' }); };
        const methods = [];
        let active=true, rebuilding=false, pendingRebuild=false, rebuildTimes=[];
        const invalid = () => {record.restore();state.windows.delete(hwnd);signal(3,hwnd);};
        const rebuild = () => {
          if (!active || window.isDestroyed()) return;
          if(rebuilding){pendingRebuild=true;return;}
          const now=Date.now();rebuildTimes=rebuildTimes.filter(t=>now-t<1000);
          if(rebuildTimes.length>=3){invalid();return;}rebuildTimes.push(now);
          rebuilding=true; signal(4,hwnd);
          // The host can replace its title tree during maximize. Restore the
          // old DOM transaction first, then validate the complete replacement.
          // A new layout that fails validation ends the lease.
          execute('attach',token).then(next=>{
            rebuilding=false;
            if(!active){execute('detach');return;}
            if(next.error || !next.overlay)throw Error(next.error||'Overlay tree changed backend');
            if(pendingRebuild){pendingRebuild=false;rebuild();return;}
            signal(1,hwnd,...next.metrics);
          }).catch(error=>{req('fs').writeFileSync(addonFile+'.error.txt',String(error));invalid();});
        };
        const navigation = (_event,_url,inPlace,mainFrame) => {if(mainFrame&&!inPlace)invalid();};
        const listener = (...args) => {
          const message = args.map(a => typeof a === 'string' ? a : a && typeof a.message === 'string' ? a.message : '').find(m => m.startsWith(token));
          if (!message || window.isDestroyed()) return;
          switch (message.slice(token.length)) { case 'close': if(window.isClosable())window.close(); break; case 'minimize': if(window.isMinimizable())window.minimize(); break; case 'maximize': if(window.isMaximizable())window.isMaximized() ? window.unmaximize() : window.maximize(); break; case 'invalid': rebuild(); break; }
        };
        record.restore = () => {active=false;window.webContents.removeListener('did-start-navigation',navigation);window.webContents.removeListener('render-process-gone',invalid);window.webContents.removeListener('console-message', listener);for(const [name,saved] of methods)saved?Object.defineProperty(window,name,saved):delete window[name];if (descriptor) Object.defineProperty(window, 'setTitleBarOverlay', descriptor); else delete window.setTitleBarOverlay; if (!window.isDestroyed()) original.call(window, settings); };
        for (const name of ['setClosable','setMinimizable','setMaximizable']) {
          const saved=Object.getOwnPropertyDescriptor(window,name), method=window[name];
          methods.push([name,saved]);
          window[name]=function(...args){const result=method.apply(this,args);execute('capabilities').catch(invalid);return result;};
        }
        window.setTitleBarOverlay = setter; setter.call(window, settings); window.webContents.on('console-message', listener);
        window.webContents.on('did-start-navigation',navigation);window.webContents.on('render-process-gone',invalid);
      }
      signal(1, hwnd, ...plan.metrics);
    }).catch(error => { record.restore(); execute('detach').finally(() => { state.windows.delete(hwnd); req('fs').writeFileSync(addonFile + '.error.txt', String(error)); signal(3, hwnd); }); });
    return 'scheduled';
  } catch (error) { return 'ERR:' + String(error); }
}
