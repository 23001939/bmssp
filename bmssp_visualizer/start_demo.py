"""Start the local server if necessary, then open the actual live demo."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import time
import urllib.request
from urllib.parse import urlencode
import webbrowser

ROOT=Path(__file__).resolve().parent


def ready(base):
    try:
        with urllib.request.urlopen(base+'/api/datasets',timeout=1) as response:
            data=json.load(response)
        return any(item.get('id')=='demo' for item in data.get('datasets',[]))
    except Exception:
        return False


def main():
    parser=argparse.ArgumentParser(description='Mở demo BMSSP cục bộ')
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--no-browser',action='store_true')
    parser.add_argument('--no-autoplay',action='store_true')
    parser.add_argument('--dataset',help='ID bộ dữ liệu có sẵn, ví dụ weighted_100')
    args=parser.parse_args()
    base=f'http://127.0.0.1:{args.port}'
    if not ready(base):
        log=(ROOT/'server.log').open('a',encoding='utf-8')
        process=subprocess.Popen([sys.executable,'-X','utf8',str(ROOT/'server.py'),'--port',str(args.port)],
             cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,
             creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (ROOT/'server.pid').write_text(str(process.pid),encoding='ascii')
        log.close()
        deadline=time.monotonic()+30
        while not ready(base):
            if process.poll() is not None or time.monotonic()>deadline:
                raise SystemExit('Không khởi động được server. Xem bmssp_visualizer/server.log.')
            time.sleep(.2)
    params={}
    if args.dataset:
        params['dataset']=args.dataset
    if not args.no_autoplay:
        params['demo']='live'
    url=base+'/'+('?' + urlencode(params) if params else '')
    print('BMSSP Visualizer:',url,flush=True)
    if not args.no_browser:
        webbrowser.open(url)


if __name__=='__main__':
    main()
