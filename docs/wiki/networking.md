# Networking and Steam

[Forge wiki](../../GUIDE.md) · **Networking and Steam**

Forge 2.9 exchanges native messages without prescribing entity replication, game rules, a server framework, or an editor. LAN works without Steam. Games decide what messages mean and validate received data.

- [Choose a transport](#choose-a-transport)
- [Host and connect](#host-and-connect)
- [Limits and diagnostics](#limits-and-diagnostics)
- [Scene ownership and reload](#scene-ownership-and-reload)
- [Build optional Valve sockets](#build-optional-valve-sockets)
- [Enable Steamworks](#enable-steamworks)
- [Steam platform API](#steam-platform-api)
- [Replace the implementation](#replace-the-implementation)

## Choose a transport

| Backend | Build requirement | Addresses and services |
| --- | --- | --- |
| `lan` | ENet, included in default bootstrap/build | Numeric IPv4, reliable ordered or unreliable sequenced UDP, independent channels; no Steam client |
| `sockets` | Optional open-source Valve GameNetworkingSockets, Protobuf, OpenSSL on macOS/Linux; BCrypt on Windows | Numeric IPv4/IPv6, reliable/unreliable IP messages; no Steam client, Steam identity, lobby, or relay service |
| `steam_ip` | Your official Steamworks SDK and a running Steam client; project explicitly enables Steam | IP messages through the Steamworks sockets interface |
| `steam` | Same SDK/client requirement | Steam ID P2P connections and virtual ports; Steam authentication/relay subject to Valve services and configuration |

`network.backends()` and `forge.capabilities()['network_backends']` return the compiled transports. Steamworks compilation is reported separately by `steam.info()['compiled']`. A compiled Steam transport still requires successful project Steam initialization.

LAN ports are opened only when game code creates a host or connects. The default project disables Steam; there is no automatic matchmaker, LAN discovery, or external service requirement. ENet does not add encryption or authenticated identity; its default use is a trusted LAN. Valve transports have their own authentication/encryption policies. Numeric IP input avoids DNS work on the game thread. Steam IDs are decimal **strings**, preserving all 64 bits across tools.

## Host and connect

```python
from network import Host

server = Host(bind="0.0.0.0", port=7777, peers=16)
client = Host()
peer = client.connect("192.168.1.20", 7777)

def on_update(dt):
    for event in client.poll():
        if event["type"] == "connected":
            accepted = client.send(peer, b"hello", channel=0, reliable=True)
            # False means backpressure: retry later, without an unbounded queue.
        elif event["type"] == "message":
            data = event["data"]  # bytes, including embedded NULs
        elif event["type"] == "disconnected":
            reason = event["reason"]
```

Use the appropriate endpoint in each game process. An empty `bind` creates a client host; a numeric bind creates a listener. Port `0` selects an ephemeral IP listener port, available through `host.stats()['port']`. Valve requires a nonzero underlying IP listener port: Forge asks the OS for one and retries up to four times if it is claimed concurrently. IPv6 examples use `bind="::1"` and `connect("::1", port)` with `sockets`/`steam_ip`.

Wait for `connected` before sending. Peer numbers belong to that host; they are never reused within its lifetime and are unrelated to entity IDs. `disconnect(peer, reason=0)` begins closure; `close()` releases the host immediately and is idempotent. A context manager closes on exit. Local disconnect notification timing and numeric end codes depend on the transport.

Messages preserve boundaries. Native payloads use `bytes`; text needs `.encode('utf-8')`. `send_json(peer, value, channel=0, reliable=True)` is an explicit convenience that rejects NaN/Infinity and uses UTF-8. Deserialize and validate your own protocol; Forge never unpickles received objects or executes remote calls. `lan` has per-channel sequencing; Valve channels are a one-byte logical prefix over its default ordered stream, so they do not promise independent congestion/ordering lanes. Unreliable packets may be dropped; ordering details follow the selected transport.

Every polled event includes `type`, `peer`, `address`, `identity`, `channel`, `reliable`, `reason`, and `error`; `message` adds `data`. Types are `connected`, `message`, `disconnected`, and transport `error`. Fields unused by a transport are empty/zero. The Valve address/identity metadata is available on connection events; message events primarily identify the peer.

Run the self-contained real LAN example:

```sh
python tools/forge.py run --headless --frames 120 --scene networking.py --no-open-log
```

It binds loopback, returns a reliable binary message, and quits. A firewall must allow a real LAN game port. `bind="127.0.0.1"` restricts an IPv4 example to this computer.

## Limits and diagnostics

Pass these optional keyword arguments to `Host`. Limits apply per host unless noted.

| Option | Default | Accepted range / meaning |
| --- | --- | --- |
| `peers` | 32 | 1..1024 |
| `channels` | 4 | 1..32; send channel index starts at 0 |
| `timeout_ms` | 10000 | 100..120000; transport connection timeout policy |
| `poll_budget` | 256 | 1..4096 native events/messages per service pass |
| `max_message_bytes` | 65536 | 1..524287 application payload bytes |
| `queue_bytes` | 4194304 | At least max message size, at most 64 MiB |
| `queue_events` | 1024 | 8..65536 application events |
| `incoming_bandwidth`, `outgoing_bandwidth` | 0 | 0..1000000000 bytes/sec; ENet bandwidth policy, 0 unlimited; Valve ignores these ENet-specific fields |

`poll(limit=256)` returns at most 1..4096 events, also servicing the host. Native temporary batches also respect the worst-case payload budget: their event count is capped by `queue_bytes / max_message_bytes` as well as `poll_budget`. The runtime services hosts every frame, including gameplay pause and failed reload recovery. Sends are accumulated for transport servicing rather than explicitly flushed for each Python call. No Python callback runs from a networking worker. All host and Steam operations must stay on their owning runtime thread. Worker-thread creation is rejected before modifying runtime services. Native hosts share one servicing thread while host objects exist, because transport callback dispatch/global initialization are shared; put custom asynchronous work inside a transport and return events on that thread.

`send` returns `False` on outgoing capacity refusal. Invalid sizes/channels, closed hosts, unavailable backends, and connection errors raise exceptions and use normal terminal/file diagnostics if uncaught. Python bytes go directly to native transport input without JSON conversion; transports own copied packets after acceptance. Reliable means transport delivery/ordering while connected, not proof that the remote game consumed a command. Use acknowledgments in your application protocol when needed.

`stats()` reports backend, closed state, port, peer RTT/pending bytes, sent/received application bytes, queued event/byte counts, drops, and overflows. ENet adds wire counters. Valve adds callback notification drops. Queue overflow drops unreliable messages; reliable receive overflow disconnects that peer and increments diagnostics. A saturated control-event queue may discard notifications: monitor counters as well as events. Poll regularly. Native transport buffers, per-peer limits, protocol headers, decoder/crypto workers, and application-owned copies mean these budgets are **not a total process-memory cap**. The runtime permits at most 1024 live hosts.

Measure your own machine with `python tools/benchmark_network.py build/bin/forge --backend lan --messages 10000 --size 256`; `--backend sockets` uses the compiled Valve adapter. Results include Python/queue/loopback work and intentional yielding, without a timing gate or game-FPS claim.

## Scene ownership and reload

The default `lifetime="scene"` closes a host when its scene retires, even if Python keeps a reference. Candidate scene hosts close after a failed load. During candidate initialization the old scene and its hosts remain alive; retirement occurs only after successful commit. Dropping the last game reference releases an unnamed host at the next runtime servicing pass; the runtime retains ownership until then so a Python worker cannot destroy its native transport.

For a session that spans scenes/reloads:

```python
session = Host(name="multiplayer", lifetime="application",
               bind="0.0.0.0", port=7777, peers=16)
```

A repeated name adopts the same host only when **all settings match**. A mismatch raises an error; it cannot silently change a live socket. Application hosts need a 1..128 character name and close at runtime shutdown. Newly created named hosts close if their candidate scene fails; previously committed hosts survive. Fixed-port scene hosts may conflict with the still-active previous scene: use a named application host for a shared listening session.

Wire effects (`send`, `connect`, `disconnect`, `poll`) and explicit closing are rejected during hot reload/authoring preparation and retired scene teardown. Candidate code can adopt/create hosts and inspect stats. Start communication in normal update callbacks after commit. `close()` during retired teardown is ignored because scene ownership handles closure and a shared session may already have been adopted. Explicitly close an application host during ordinary execution to replace its options. Shutdown closes everything before Steam API shutdown.

These rules cover managed host ownership; previously sent packets, Steam services, arbitrary Python sockets/file I/O, and remote game state cannot be rolled back. Steam settings require restart; edits during hot reload are rejected with the previous runtime retained.

## Build optional Valve sockets

The open-source Valve library is distinct from the Steamworks SDK. It can provide Steam-style **IP LAN sockets without Steam**, but cannot impersonate Steam users or grant authenticated P2P/relay access.

Build pinned private dependencies in `.tools/network` (requires Perl/make on macOS/Linux or the Visual Studio developer environment on Windows):

```sh
python tools/network_dependencies.py
python tools/forge.py compile --sockets
```

The bootstrap verifies TLS/SHA-256 and builds Protobuf 3.21.12 plus OpenSSL 3.5.9 on macOS/Linux; Windows uses native BCrypt instead of OpenSSL. On macOS its default deployment target is 11.0; choose `--deployment-target` to match your target. Keep architecture/compiler consistent with Forge. These are source-build versions, not a promise of current security support; maintain crypto dependencies for your shipped target.

Alternatively install compatible Protobuf/protoc and OpenSSL with your toolchain, fetch `python tools/dependencies.py --sockets`, and use CMake `FORGE_WITH_GNS=ON`, `CMAKE_PREFIX_PATH`, `OPENSSL_ROOT_DIR`, `OPENSSL_USE_STATIC_LIBS=TRUE`, and `Protobuf_USE_STATIC_LIBS=ON` as appropriate. CMake never downloads dependencies. Forge disables the standalone library's ICE/examples/tools: this backend is direct IP, without automatic NAT traversal or external signaling.

`compile --without-networking` removes ENet independently. `--sockets` does not require the editor or a native graphics backend. Leaving `--sockets` off on a later launcher compilation disables the optional Valve backend. Static private dependencies simplify standalone packaging; externally selected shared dependencies require packaging validation for your platform.

## Enable Steamworks

The official SDK is not downloaded, accepted, or redistributed by Forge. Obtain it from Valve under your own Steamworks access/terms. It must contain `public/steam/steam_api.h` and the 64-bit platform runtime under `redistributable_bin`.

```sh
python tools/forge.py compile --steamworks-sdk /path/to/steamworks/sdk
```

On Windows pass the SDK path in Developer PowerShell. CMake flags are `FORGE_WITH_STEAMWORKS=ON` and `FORGE_STEAMWORKS_SDK=<root>`. This is independent of `--sockets`, ENet, and the editor. A Steam-enabled executable links Valve's platform redistributable even when a particular project disables initialization; the ordinary build has no SDK dependency.

Configure each project's `engine.json`:

```json
"steam": {
  "enabled": false,
  "app_id": 480,
  "relay": false
}
```

`enabled=false` is the default and turns off Steam initialization. Set it to `true` explicitly when testing the Steam build, with the Steam client running. `app_id` is a positive uint32 integer. **480 is Valve's Spacewar testing ID**, not an ID for publishing your own game; replace it with your assigned App ID for that game. Changing this field never requires recompiling C++. `relay` prewarms Valve relay access when enabled; it does not disable the socket library's own P2P routing when false.

Forge sets process-local `SteamAppId`/`SteamGameId` for initialization, checks the resulting App ID, and restores previous environment values at shutdown. It does not create or ship `steam_appid.txt`. Steam initialization failure is explicit, with terminal/log diagnostics. Validation/build reject `enabled=true` when the current runtime lacks the SDK adapter, without initializing Steam or creating an unusable package. The service cannot be enabled after startup or reconfigured by hot reload.

For IP LAN select `Host(backend="steam_ip", bind="0.0.0.0", port=7777)`. For Steam P2P create a listener with `Host(backend="steam", bind="listen", port=0)`; the bind text is a listening switch for P2P, not an IP address. Connect with `Host(backend="steam").connect(remote_steam_id, virtual_port)`. Peers must use compatible App IDs/virtual ports and satisfy Valve/account/network requirements. Discover Steam IDs through your game/lobbies; Forge does not automatically admit authorized players or define a lobby protocol.

Packaging copies the compiled SDK runtime (`steam_api64.dll` on Windows or relocated/signed `libsteam_api.dylib` on macOS). It copies networking notices and hashes files in the manifest; it never bundles SDK headers, credentials, or an App ID marker. You remain responsible for applicable Valve redistribution terms and Steam publication setup. Verify a package from a different working directory on its target OS with a running Steam client.

## Steam platform API

`import steam` exposes independent convenience functions over the native service. When disabled, `info()` and `poll()` remain useful; service calls raise a clear error. `info()` reports compile/enable state and configured App ID; an initialized client also reports Steam ID, persona name, logged-on state, and overlay availability.

| Function | Result / behavior |
| --- | --- |
| `friends()` | Friend Steam ID strings, names and numeric persona state |
| `overlay(dialog="friends")` | Opens a supported Valve overlay dialog; availability depends on Steam launch/device integration |
| `presence(key, value)` | Sets a bounded rich-presence entry; returns SDK acceptance |
| `achievement(name)`, `unlock(name)` | Reads/unlocks an achievement configured for the App ID |
| `stat(name, kind="int")`, `set_stat(name, value, kind="int")` | Integer/float stats; names/types must match the Steam definition |
| `store_stats()` | Requests persistence; acceptance is not the asynchronous completion result |
| `create_lobby(members=4, visibility=2)` | Async request token string; visibility: private 0, friends 1, public 2, invisible 3 |
| `list_lobbies(limit=50)`, `join_lobby(lobby)` | Async request token string |
| `leave_lobby(lobby)`, `lobby_info(lobby)` | Leave or query owner/member ID strings |
| `set_lobby_data(lobby, key, value)`, `lobby_data(lobby, key)` | Set/read bounded string metadata, subject to Steam ownership rules |
| `poll(limit=256)` | Drains bounded platform completion/events |

```python
import steam

request = None

def on_update(dt):
    global request
    for event in steam.poll():
        if event["type"] == "stats_ready" and event["ok"]:
            if not steam.achievement("ACH_WIN_ONE_GAME"):
                steam.unlock("ACH_WIN_ONE_GAME")
                steam.store_stats()
        elif event["type"] == "lobby_create" and event["ok"]:
            lobby = event["lobby"]
    if request is None:
        request = steam.create_lobby(members=4)
```

Wait for `stats_ready` before accessing achievements/stats (legacy SDKs request stats; newer interfaces may use Steam-preloaded state); names are App-ID-specific. Events include `stats_ready`, `stats_stored`, `overlay`, `lobby_create`, `lobby_join`, and `lobby_list`. Async lobby events include `request` and `ok`; successful results add lobby/list data, and applicable errors add the SDK numeric `result`. Inspect completion results, not just initial request acceptance. Request IDs are strings. The service limits outstanding lobby requests to 128, queued events to 1024, and callbacks processed per pass to 256; `info()['dropped_events']` reports event overflow. SDK buffers/network memory are separate. No callback runs Python directly.

Live-client checks are explicit: `python tests/steam_client.py build/bin/forge --app-id 480`; add `--lobbies` to create/join/leave a private test lobby. These are excluded from CI and require your SDK-enabled binary/client. They do not certify overlay pixels or two-account P2P/relay.

This integration does not implement Steam game servers, Workshop, inventory, payments, Remote Storage/Cloud, voice, or a game replication protocol. Add these independently through the public extension path. The official SDK/client branch must be compiled and tested on your target: open-source Valve LAN tests do not verify Steam authentication, overlay, achievements, matchmaking, or relay.

## Replace the implementation

`modules/network.py` and `modules/steam.py` are optional editable facades. The native API is `forge.network_host(settings, name="", lifetime="scene")`, `forge.network_backends()`, `forge.steam_info()`, `forge.steam_poll(limit)`, and `forge.steam_call(operation, arguments={})`.

`engine/include/forge/network.hpp` defines the public `NetworkTransport` factory boundary: connect/send/disconnect/service/stats. Register a unique name with `registerNetworkTransport` before host creation through a normal `FORGE_MODULE` extension. See [network_transport_example.cpp](../../modules/network_transport_example.cpp). Optional `transport_settings` passes a JSON object (at most 16 KiB, 4096 values, depth 64, finite numbers) to `NetworkOptions.transportSettings` for custom factories; builtin transports reject nonempty custom settings. The example accepts `transport_settings={"label":"My transport"}` and exposes that label in stats. Custom settings participate in named-session equality. Preserve owning-thread operation, bounded queues, message limits, stable peer IDs, byte ownership, and exception-safe cleanup. Do not block the frame on DNS/I/O. The example's echo backend is an extension demonstration, not network traffic.

Networking/Steam adapters, their headers, facades, and build helpers are adaptable independent components. Core changes only connect their lifetime/configuration/capabilities/packaging contracts. Changing a transport alone does not modify the licensed Core boundary. [Tests and verification](development.md#validation-and-test-suites) include actual native LAN exchanges and rejected reloads; Steam-client testing is separate.

Primary references: [Valve open-source sockets](https://github.com/ValveSoftware/GameNetworkingSockets), [Steamworks API setup](https://partner.steamgames.com/doc/sdk/api), [Steam sockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets), [Steam matchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking), [Steam stats](https://partner.steamgames.com/doc/api/ISteamUserStats), [ENet](https://github.com/lsalzman/enet).
