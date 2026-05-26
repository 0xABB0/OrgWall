import functools
import http.server
import socketserver
import sys

directory = sys.argv[1]
port = int(sys.argv[2])
cross_origin_isolated = len(sys.argv) > 3 and sys.argv[3] == "1"


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # SharedArrayBuffer (web threading) demands a cross-origin-isolated
        # context; without threading the headers are harmless.
        if cross_origin_isolated:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


with socketserver.TCPServer(("", port), functools.partial(Handler, directory=directory)) as httpd:
    httpd.allow_reuse_address = True
    print(f"serving {directory} at http://localhost:{port} (ctrl-c to stop)", flush=True)
    httpd.serve_forever()
