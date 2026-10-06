"""Optional native Steamworks facade. Configure steam.enabled/app_id in engine.json."""
import forge


def info(): return forge.steam_info()
def poll(limit=256): return forge.steam_poll(limit)
def friends(): return forge.steam_call("friends")
def overlay(dialog="friends"): return forge.steam_call("overlay", {"dialog": dialog})
def presence(key, value): return forge.steam_call("presence", {"key": key, "value": value})
def achievement(name): return forge.steam_call("achievement_get", {"name": name})
def unlock(name): return forge.steam_call("achievement_set", {"name": name})
def stat(name, kind="int"): return forge.steam_call("stat_get", {"name": name, "kind": kind})
def set_stat(name, value, kind="int"): return forge.steam_call("stat_set", {"name": name, "value": value, "kind": kind})
def store_stats(): return forge.steam_call("stats_store")
def create_lobby(members=4, visibility=2): return forge.steam_call("lobby_create", {"members": members, "visibility": visibility})
def list_lobbies(limit=50): return forge.steam_call("lobby_list", {"limit": limit})
def join_lobby(lobby): return forge.steam_call("lobby_join", {"lobby": lobby})
def leave_lobby(lobby): return forge.steam_call("lobby_leave", {"lobby": lobby})
def lobby_info(lobby): return forge.steam_call("lobby_info", {"lobby": lobby})
def set_lobby_data(lobby, key, value): return forge.steam_call("lobby_set", {"lobby": lobby, "key": key, "value": value})
def lobby_data(lobby, key): return forge.steam_call("lobby_get", {"lobby": lobby, "key": key})
