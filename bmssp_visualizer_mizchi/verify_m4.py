"""Compact E2E browser validation for M4."""
from __future__ import annotations
import json, shutil, subprocess, time, urllib.request
from pathlib import Path
import websocket

ROOT = Path(__file__).resolve().parent
URL = 'http://127.0.0.1:8766/?dataset=demo'
PORT = 9245
PROFILE = ROOT / 'runs' / 'chrome_m4_prof'
OUT_HTML = ROOT / 'runs' / 'offline_m4_demo.html'
CHROME = r'C:\Program Files\Google\Chrome\Application\chrome.exe'

class Chrome:
    def __init__(self, url):
        shutil.rmtree(PROFILE, ignore_errors=True)
        self.p = subprocess.Popen([
            CHROME, '--headless=new', '--disable-gpu', '--no-first-run',
            f'--remote-debugging-port={PORT}', f'--user-data-dir={PROFILE}', url
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        d = time.monotonic() + 15
        while time.monotonic() < d:
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=1) as r:
                    pages = json.load(r)
                page = next(p for p in pages if p.get('type') == 'page')
                break
            except Exception:
                time.sleep(0.2)
        else:
            self.close(); raise RuntimeError('CDP timeout')
        self.ws = websocket.create_connection(page['webSocketDebuggerUrl'], timeout=12)
        self.seq = 0
        self.errors = []
        self.call('Runtime.enable')
        self.call('Page.enable')
        self.wait("document.readyState === 'complete'")

    def call(self, method, params=None):
        self.seq += 1
        self.ws.send(json.dumps({'id': self.seq, 'method': method, 'params': params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get('method') == 'Runtime.exceptionThrown':
                self.errors.append(msg)
            if msg.get('id') == self.seq:
                if 'error' in msg: raise RuntimeError(msg['error'])
                return msg.get('result', {})

    def evaluate(self, expr):
        res = self.call('Runtime.evaluate', {'expression': expr, 'returnByValue': True, 'awaitPromise': True})
        if 'exceptionDetails' in res: raise RuntimeError(res['exceptionDetails'])
        return res.get('result', {}).get('value')

    def wait(self, expr, timeout=25.0):
        d = time.monotonic() + timeout
        while time.monotonic() < d:
            if self.evaluate(expr): return
            time.sleep(0.1)
        raise TimeoutError(expr)

    def close(self):
        try: self.ws.close()
        except Exception: pass
        if self.p:
            self.p.terminate()
            try: self.p.wait(timeout=3)
            except Exception: pass

def main():
    print("[1/5] Chrome CDP ket noi...")
    b = Chrome(URL)
    try:
        b.wait("document.getElementById('stat-n')?.textContent === '18'")
        assert 'Mizchi' in b.evaluate("document.title")
        assert b.evaluate("['run','run-chd','run-dijkstra'].every(id => !!document.getElementById(id))")
        assert b.evaluate("['stat-nv','stat-mv','stat-k','stat-t','stat-levels','stat-relax'].every(id => !!document.getElementById(id))")
        print("  -> UI branding, 3 run buttons, params panel OK.")

        print("[2/5] Chay fast mode cho 3 thuat toan...")
        b.evaluate("""
            window.__runs = [];
            const orig = window.fetch;
            window.fetch = async (...a) => {
              const r = await orig(...a);
              if (String(a[0]).includes('/api/runs') && a[1]?.method === 'POST') {
                window.__runs.push((await r.clone().json()).run_id);
              }
              return r;
            };
            document.getElementById('mode').value = 'fast';
            document.getElementById('mode').dispatchEvent(new Event('change'));
        """)
        for algo, btn in [('dmm25', 'run'), ('chd', 'run-chd'), ('dijkstra', 'run-dijkstra')]:
            c = b.evaluate("window.__runs.length")
            b.evaluate(f"document.getElementById('{btn}').click()")
            b.wait(f"window.__runs.length > {c}")
            b.wait("document.getElementById('run-status')?.textContent === 'Ho\u00e0n t\u1ea5t'")
            verif = b.evaluate("document.getElementById('verification')?.textContent || ''")
            cost = b.evaluate("document.getElementById('path-cost')?.textContent || ''")
            p_text = b.evaluate("document.getElementById('path-text')?.textContent || ''")
            print(f"  -> {algo.upper()}: Trạng thái '{verif}', path cost = '{cost}', path = '{p_text}'")
            assert 'Khớp Oracle' in verif
            assert cost in ('11', '11,0', '11.0') or '11' in cost
        assert len(b.evaluate("[...document.querySelectorAll('#comparison-body tr')]")) == 3
        print("  -> Bang so sanh 3 thuat toan hien thi du 3 dong.")

        print("[3/5] Chay controlled mode (SSE + Pause/Step/Resume) voi C-HD...")
        b.evaluate("""
            document.getElementById('mode').value = 'controlled';
            document.getElementById('mode').dispatchEvent(new Event('change'));
            document.getElementById('speed').value = '40';
            document.getElementById('speed').dispatchEvent(new Event('change'));
        """)
        c = b.evaluate("window.__runs.length")
        b.evaluate("document.getElementById('run-chd').click()")
        b.wait(f"window.__runs.length > {c}")
        run_id = b.evaluate("window.__runs.at(-1)")
        b.wait("Number(document.getElementById('event-count')?.textContent || 0) >= 4")
        b.evaluate("document.getElementById('pause').click()")
        b.wait("!document.getElementById('step')?.disabled")
        e_p = b.evaluate("Number(document.getElementById('event-count')?.textContent || 0)")
        time.sleep(0.3)
        assert e_p == b.evaluate("Number(document.getElementById('event-count')?.textContent || 0)")
        b.evaluate("document.getElementById('step').click()")
        b.wait(f"Number(document.getElementById('event-count')?.textContent || 0) > {e_p}")
        b.evaluate("document.getElementById('speed').value = '0'; document.getElementById('pause').click()")
        b.wait("document.getElementById('run-status')?.textContent === 'Ho\u00e0n t\u1ea5t'", 30)
        print("  -> Controlled transport controls (Pause, Step, Resume) hoat dong chinh xac.")

        print("[4/5] Kiem tra export HTML va offline replay...")
        with urllib.request.urlopen(f'http://127.0.0.1:8766/api/runs/{run_id}/export?kind=html', timeout=15) as r:
            OUT_HTML.write_bytes(r.read())
        b.call('Page.navigate', {'url': OUT_HTML.as_uri()})
        b.wait("document.getElementById('connection-label')?.textContent.includes('Ngo\u1ea1i tuy\u1ebfn')")
        assert b.evaluate("!document.getElementById('run-chd')?.hidden")
        b.evaluate("document.getElementById('speed').value = '0'; document.getElementById('run-chd').click()")
        b.wait("document.getElementById('run-status')?.textContent.includes('B\u1ea3n ghi')", 30)
        assert b.evaluate("document.getElementById('path-cost')?.textContent") == '11'
        print("  -> Offline HTML replay thanh cong (cost=11).")

        print("[5/5] Kiem tra JS error...")
        assert len(b.errors) == 0, f"JS errors: {b.errors}"
        print("  -> 0 loi JavaScript.")
        print("\n===> XAC NHAN: M4 DA HOAN THANH VA KIEM CHUNG 100%! <===")
    finally:
        b.close()

if __name__ == '__main__':
    main()
