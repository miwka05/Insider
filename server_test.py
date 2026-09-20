from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/metrics":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(length)

        try:
            payload = json.loads(raw_body.decode("utf-8"))
        except json.JSONDecodeError:
            self.send_error(400, "Invalid JSON")
            return

        print("\n=== POST /metrics ===")
        print(json.dumps(payload, ensure_ascii=False, indent=2))

        self.send_response(self.server.response_status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.end_headers()

        response = {"ok": 200 <= self.server.response_status < 300}
        self.wfile.write(json.dumps(response).encode("utf-8"))

    def log_message(self, format, *args):
        # Keep the console readable: payload is already printed above.
        return


def main():
    parser = argparse.ArgumentParser(description="Local demo server for Insider monitoring agent")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--status", type=int, default=200,
                        help="HTTP status to return for POST requests")
    args = parser.parse_args()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.response_status = args.status

    print(f"Listening on http://127.0.0.1:{args.port}/metrics")
    print(f"POST responses will use HTTP {args.status}")
    print("Press Ctrl+C to stop the server.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
