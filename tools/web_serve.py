#!/usr/bin/env python3
"""Local test server for the web page (<build>/site) with the cross-origin isolation headers GitHub Pages lacks
(there the service worker adds them, web/sw.js).

usage: python tools/web_serve.py [port] [build_web]
EH_DEV_APK=<path>: also serves that APK at /__dev_apk, for driving the page in a browser that has no file dialog
(local tests only; the published page never has it).
"""
import functools, http.server, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
SITE = os.path.join(ROOT, sys.argv[2] if len(sys.argv) > 2 else 'build_web', 'site')


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, '.wasm': 'application/wasm',
                      '.webmanifest': 'application/manifest+json', '.js': 'text/javascript'}

    def do_GET(self):
        apk = os.environ.get('EH_DEV_APK')
        if apk and self.path == '/__dev_apk':
            data = open(apk, 'rb').read()
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


print(f'http://localhost:{PORT}/  ({SITE})')
http.server.ThreadingHTTPServer(('127.0.0.1', PORT), functools.partial(Handler, directory=SITE)).serve_forever()
