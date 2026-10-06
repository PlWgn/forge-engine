"""Real loopback LAN, no Steam/editor required. Select --scene networking.py."""
import forge
from network import Host


def build():
    return {"mode": "2d", "physics_enabled": False}


def on_start():
    global server, client, client_peer, done
    server = Host(bind="127.0.0.1", port=0)
    client = Host()
    client_peer = client.connect("127.0.0.1", server.stats()["port"])
    done = False
    forge.log("LAN example: connecting to local port " + str(server.stats()["port"]))


def on_update(dt):
    global done
    for event in client.poll():
        if event["type"] == "connected":
            client.send(client_peer, b"Forge LAN ping\0", channel=1)
        elif event["type"] == "message":
            assert event["data"] == b"Forge LAN ping\0"
            done = True
            forge.log("LAN example: binary reliable message returned successfully")
            forge.quit()
    for event in server.poll():
        if event["type"] == "message":
            server.send(event["peer"], event["data"], channel=event["channel"])
