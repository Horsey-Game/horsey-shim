import re, sys
sigs = {
    "game_frame": "40 53 48 83 EC 40 0F 29 74 24 30 0F 29 7C 24 20 E8 ? ? ? ? 80 3D ? ? ? ? 00 0F 57 C0 F3 0F 2A 05 ? ? ? ?",
    "player_add_money": "48 83 EC 58 89 91 10 03 00 00 01 91 08 03 00 00 C7 81 0C 03 00 00 3C 00 00 00 45 84 C0 0F 85",
    "spend_money": "40 53 48 83 EC 50 44 8B 91 08 03 00 00 44 3B D2 0F 8C ? ? ? ? 8B C2 F7 D8 89 81 10 03 00 00 44 2B D2 44 89 91 08 03 00 00 C7 81 0C 03 00 00",
    "horse_records_ref": "48 83 EC 28 83 F9 FF 75 18 48 8D 0D ? ? ? ? E8 ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? 48 63 C1 48 69 C0 88 00 00 00 48 03 05 ? ? ? ?",
    "world_obj_ref": "48 8B 0D ? ? ? ? 42 80 7C 07 09 00 74 12 45 33 C0 BA FA 00 00 00 E8",
    "world_spawn_herd": "48 89 5C 24 08 4C 89 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 49 8B F9 41 8B E8 8B DA 4C 8B F9 48 8B B4 24 A0 00 00 00 44 8B 94 24 90 00 00 00 45 85 D2 0F 8E",
    "pop_map_ref": "48 89 5C 24 18 56 57 41 57 48 81 EC 80 00 00 00 48 89 AC 24 A0 00 00 00 48 8B F2 48 8B 2D ? ? ? ? 4C 8B F9 48 8B DD 48 8B 7D 08 80 7F 19 00",
}
data = open(r"D:\SteamLibrary\steamapps\common\Horsey Game\Horsey.exe", "rb").read()
for name, pat in sigs.items():
    rx = b"".join(b"." if t == "?" else re.escape(bytes([int(t, 16)])) for t in pat.split())
    hits = [m.start() for m in re.finditer(rx, data, re.S)]
    print(name, len(hits), [hex(h) for h in hits])
