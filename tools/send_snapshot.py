"""Send one localhost trigger; wait for confirmation or a capture error."""
import json
import socket


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.bind(("127.0.0.1", 0))
        client.settimeout(2)
        client.sendto(b"SNAPSHOT", ("127.0.0.1", 9003))
        try:
            while True:
                packet, peer = client.recvfrom(65535)
                if peer != ("127.0.0.1", 9003):
                    continue
                result = json.loads(packet)
                if result["type"] == "snapshot_pending":
                    print(f"Capture {result['id']} queued")
                elif result["type"] == "snapshot":
                    print(f"Capture {result['id']} frozen; gap={result['gapMs']:.2f} ms, alpha={result['alpha']:.4f}")
                    return 0
                elif result["type"] == "snapshot_error":
                    print(f"Capture failed: {result['message']}")
                    return 1
        except socket.timeout:
            print("No final reply. Check that the backend is running and UDP port 9003 is available.")
            return 1


if __name__ == "__main__":
    raise SystemExit(main())
