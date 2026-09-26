#!/usr/bin/env python3
"""Minimal CAS-like server for testing sign-in callback capture.

/start          login page; submitting the form POSTs to /login
/login  (POST)  302 -> /cas/callback?ticket=ST-... (server-side redirect)
/js             page that navigates to /cas/callback?ticket=... via script
/cas/callback   must NOT be reached when capture happens before the request

Every request is printed, so the output shows whether the ticket URL was
actually requested by the browser.
Usage: python tools/fake-cas-server.py [port]
"""
import http.server
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

LOGIN_PAGE = b"""<!DOCTYPE html><html><head><meta charset="utf-8"><title>Fake CAS</title></head>
<body style="font-family:Segoe UI;margin:40px">
<h2>Fake CAS sign-in</h2>
<form method="post" action="/login">
<input name="user" value="alice"> <input name="pass" type="password" value="x">
<button id="go" type="submit">Sign in</button>
</form>
<script>setTimeout(function(){document.getElementById('go').click();}, 1500);</script>
</body></html>"""

JS_PAGE = b"""<!DOCTYPE html><html><body>
<script>setTimeout(function(){location.href='/cas/callback?ticket=ST-js-123';}, 1000);</script>
redirecting by script...</body></html>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("REQUEST", self.command, self.path, flush=True)

    def _send(self, code, body=b"", headers=None):
        self.send_response(code)
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/start"):
            self._send(200, LOGIN_PAGE, {"Content-Type": "text/html; charset=utf-8"})
        elif self.path.startswith("/js"):
            self._send(200, JS_PAGE, {"Content-Type": "text/html"})
        elif self.path.startswith("/cas/callback"):
            print("!!! TICKET URL REACHED THE SERVER:", self.path, flush=True)
            self._send(200, b"ticket consumed", {"Content-Type": "text/plain"})
        else:
            self._send(404, b"not found")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        self.rfile.read(length)
        if self.path.startswith("/login"):
            self._send(302, b"", {"Location": f"http://127.0.0.1:{PORT}/cas/callback?ticket=ST-redirect-456"})
        else:
            self._send(404, b"not found")


http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
