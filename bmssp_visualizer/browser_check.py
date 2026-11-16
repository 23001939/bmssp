"""Local Chrome CDP checks using the installed browser, no browser download."""
from __future__ import annotations
import base64
import json
from pathlib import Path
import subprocess
import time
import urllib.request
import websocket

ROOT=Path(__file__).resolve().parent


class Browser:
    def __init__(self,url='http://127.0.0.1:8765',port=9228):
        self.port=port
        self.process=subprocess.Popen([
            r'C:\Program Files\Google\Chrome\Application\chrome.exe',
            '--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check',
            '--disable-background-networking','--disable-extensions',
            '--remote-allow-origins=*',f'--remote-debugging-port={port}',
            f'--user-data-dir={ROOT / "browser_profile"}',url],
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        deadline=time.monotonic()+15
        while True:
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json',timeout=1) as f:
                    pages=json.load(f)
                page=next(p for p in pages if p.get('type')=='page')
                break
            except Exception:
                if time.monotonic()>deadline:
                    self.close()
                    raise RuntimeError('Chrome CDP chưa sẵn sàng.')
                time.sleep(.2)
        self.ws=websocket.create_connection(page['webSocketDebuggerUrl'],timeout=10)
        self.counter=0
        self.errors=[]
        self.call('Runtime.enable')
        self.call('Page.enable')
        self.call('Emulation.setDeviceMetricsOverride',{'width':1440,'height':1050,'deviceScaleFactor':1,'mobile':False})
        self.wait("document.readyState==='complete'")

    def call(self,method,params=None):
        self.counter+=1
        self.ws.send(json.dumps({'id':self.counter,'method':method,'params':params or {}}))
        while True:
            result=json.loads(self.ws.recv())
            if result.get('method')=='Runtime.exceptionThrown':
                self.errors.append(result)
            if result.get('id')==self.counter:
                if 'error' in result:
                    raise RuntimeError(result['error'])
                return result.get('result',{})

    def evaluate(self,expression):
        result=self.call('Runtime.evaluate',{'expression':expression,'returnByValue':True,'awaitPromise':True})
        if 'exceptionDetails' in result:
            raise RuntimeError(result['exceptionDetails'])
        return result.get('result',{}).get('value')

    def wait(self,expression,timeout=12):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if self.evaluate(expression):
                return
            time.sleep(.1)
        raise TimeoutError(expression)

    def screenshot(self,path):
        result=self.call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
        Path(path).write_bytes(base64.b64decode(result['data']))

    def close(self):
        if hasattr(self,'ws'):
            try:
                self.call('Browser.close')
            except Exception:
                pass
            self.ws.close()
        if hasattr(self,'process'):
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()


if __name__=='__main__':
    import sys
    browser=Browser(sys.argv[1] if len(sys.argv)>1 else 'http://127.0.0.1:8765')
    try:
        browser.wait("document.querySelectorAll('svg circle').length>0 || document.body.innerText.includes('18')")
        browser.screenshot(ROOT/'preview.png')
        print(json.dumps({'title':browser.evaluate('document.title'),
                          'body':browser.evaluate('document.body.innerText').replace('\n',' ')[:1500],
                          'errors':browser.errors},ensure_ascii=False))
    finally:
        browser.close()
