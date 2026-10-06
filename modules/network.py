"""Replaceable message transport facade. Native hosts exchange bytes, never game objects."""
import json
import forge


def backends():
    return forge.network_backends()


class Host:
    """Scene-owned by default; named application hosts survive successful scene reloads."""
    def __init__(self, *, backend="lan", bind="", port=0, name="", lifetime="scene", **limits):
        self.native = forge.network_host(dict(limits, backend=backend, bind=bind, port=port), name, lifetime)

    def connect(self, address, port=0):
        return self.native.connect(address, port)

    def send(self, peer, data, *, channel=0, reliable=True):
        if not isinstance(data, bytes):
            raise TypeError("send requires bytes; encode text or use send_json explicitly")
        return self.native.send(peer, data, channel, reliable)

    def send_json(self, peer, value, *, channel=0, reliable=True):
        # No pickle or automatic remote method calls. Receiver validates its own schema.
        data = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
        return self.send(peer, data, channel=channel, reliable=reliable)

    def poll(self, limit=256):
        return self.native.poll(limit)

    def disconnect(self, peer, reason=0):
        self.native.disconnect(peer, reason)

    def stats(self):
        return self.native.stats()

    @property
    def closed(self):
        return self.native.closed

    def close(self):
        self.native.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
