# Packet System

> Status: **Phase 2**. ✅ = verified in source · ⚠️ = inferred · ❓ = unknown.
> The transport (FIFOs, `do_sockets`) is covered in [networking.md](networking.md).

## 1. Wire format

Every packet, client or inter-server, starts with a little-endian `uint16` **packet ID**:

```
fixed length:     [id.W][payload...]                     length known from a table
variable length:  [id.W][len.W][payload...]              len includes the 4-byte header
```

`doc/packet_client.txt`, `doc/packet_interserv.txt` and `doc/packet_struct_notation.md` describe individual packets. This document covers the code machinery.

## 2. Compile-time version selection

The RO client protocol changes with every client build date. rAthena supports one protocol per **build**:

| Macro | Defined in | This fork |
|---|---|---|
| `PACKETVER` | `src/custom/defines_pre.hpp` override, otherwise the default in `src/config/packets.hpp:16` | ✅ **20220406** |
| `PACKETVER_RE` / `PACKETVER_RE_NUM` | `config/packets.hpp:18-41`: set for RagexeRE dates (2015-11 → 2018-07, 2020-09-02 → 2021-11-18) | not RE: 20220406 falls outside both ranges |
| `PACKETVER_MAIN_NUM` | same | ✅ = 20220406 |
| `PACKETVER_ZERO` / `_NUM` | for Zero clients (⚠️ via build option) | unset |
| `PACKET_OBFUSCATION` | `config/packets.hpp:43-56`, on by default for `PACKETVER >= 20110817` | defined, but keys are 0 (see §5) |

Code checks these with `#if PACKETVER >= …` / `PACKETVER_MAIN_NUM >= …` all over the place: packet IDs, struct layouts, and even whether a feature exists (for example `WEB_SERVER_ENABLE`). **Changing the client date means rebuilding all servers**, and possibly fixing packets that newer clients changed.

## 3. Packet definition files

| File | Size | Role |
|---|---|---|
| `src/common/packets.hpp` | 742 lines | Packed structs + `HEADER_*` constants for **login and char** client packets (`PACKET_CA_LOGIN`, `PACKET_HC_NOTIFY_ZONESVR`, …), 63 `DEFINE_PACKET_HEADER`s; the `PacketDatabase<T>` template (`:653`) |
| `src/map/packets.hpp` | 2275 lines | Packed structs for map packets, 298 `DEFINE_PACKET_HEADER`s; includes `packets_struct.hpp` |
| `src/map/packets_struct.hpp` | 5906 lines | Older struct definitions and version-dependent header IDs (⚠️ legacy file, being merged into `packets.hpp`) |
| `src/map/clif_packetdb.hpp` | 2049 lines | **Inbound** map packet table: `packet(id, len)` and `parseable_packet(id, len, handler, pos...)` inside `#if PACKETVER` blocks |
| `src/map/clif_shuffle.hpp` | 4763 lines | Per-client-date **shuffled** packet IDs (official anti-bot measure), overriding entries in the packet DB |
| `src/map/clif_obfuscation.hpp` | 430 lines | Per-client-date obfuscation keys (`packet_keys(k1,k2,k3)`) |

Struct conventions (`src/map/packets.hpp`):

```cpp
#define DEFINE_PACKET_HEADER(name, id) const int16 HEADER_##name = id;
#pragma pack(push, 1)
struct PACKET_ZC_USER_COUNT{ uint16 packetType; int32 playersCount; } __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_USER_COUNT, 0xc2)       // packets.hpp:39
```

Naming follows the official prefixes: `CA_` client→account (login), `AC_` login→client, `CH_`/`HC_` client↔char, `CZ_`/`ZC_` client↔zone (map), `CT_`/`TC_` token/OTP. A variable-length struct has `uint16 packetLength` as its second field and a flexible array member at the end.

## 4. Inbound dispatch

### 4.1 Login & char: `PacketDatabase<T>` (modern)

✅ `src/common/packets.hpp:653-740`:

```cpp
class LoginPacketDatabase : public PacketDatabase<login_session_data>{
  LoginPacketDatabase(){
    this->add( HEADER_CA_LOGIN, true, sizeof( PACKET_CA_LOGIN ), logclif_parse_reqauth_raw<PACKET_CA_LOGIN> );
    ...
```

- `add(type, fixed, size, std::function<bool(int32 fd, T& sd)>)` registers a handler. Duplicates and sizes that are too small produce an error.
- `handle(fd, sd)`:
  1. Look up the ID; an unknown ID causes `set_eof`.
  2. Check the size. If fewer bytes are available than the fixed size, or the dynamic minimum, or `packetLength`, it calls `set_eof`.
  3. Call `func(fd, sd)`, then `RFIFOSKIP` the fixed size or `packetLength`.
- The handler reads the packet with `reinterpret_cast<const PACKET_X*>(RFIFOP(fd, 0))`.
- Instances: `LoginPacketDatabase` (`loginclif.cpp:483`), `CharPacketDatabase` (`char_clif.cpp:1595`). The char parser still has hard-coded `switch` cases for `0x65` and `0x2af8` before it falls back to the DB (`char_clif.cpp:~1660-1700`).

### 4.2 Map: `packet_db[]` (legacy table, partly typed)

✅ `src/map/clif.cpp`:

```cpp
struct s_packet_db packet_db[MAX_PACKET_DB + 1];       // clif.cpp:81
// s_packet_db { int16 len; void (*func)(int32 fd, map_session_data* sd); int32 pos[MAX_PACKET_POS]; }
```

- `packetdb_readdb()` (`clif.cpp:25830`) zeroes the table, then **`#include`s** `clif_packetdb.hpp` and `clif_shuffle.hpp` inside the function body. Their `packet(...)` / `parseable_packet(...)` macros expand to `packetdb_addpacket(cmd, len, func, pos..., 0)` (`clif.cpp:25798`). Because entries are evaluated in file order inside `#if PACKETVER` blocks, **later entries overwrite earlier ones**. That is how a packet's ID, length or field offsets are redefined for newer clients.
- `pos[]` holds per-version field offsets. Legacy handlers read fields through them: `RFIFOL(fd, packet_db[cmd].pos[0])` (e.g. `clif_parse_WantToConnection`, `clif_parse_ChangeDir` `clif.cpp:11607`).
- Modern entries use the typed form, `parseable_packet( HEADER_CZ_REQ_WEAR_EQUIP, sizeof( PACKET_CZ_REQ_WEAR_EQUIP ), clif_parse_EquipItem, 0 )`. ✅ 135 of the 774 `parseable_packet` lines use `HEADER_*`. Their handlers cast the buffer to the struct (`clif_parse_EquipItem`, `clif.cpp:12111`).
- `clif_parse` (`clif.cpp:25657`) does the dispatch. See networking.md for the gating rules. `len == -1` means variable length, taken from the second WORD.
- `packet_len(cmd)` / `packet_db[cmd].len` is also used on the **send** side when the code builds packets with `WFIFO*` (e.g. `WFIFOHEAD(fd, packet_len(0x283))`).

### 4.3 Inter-server dispatch

| Receiver | Function | Mechanism | Range |
|---|---|---|---|
| map ← char (control) | `chrif_parse` (`chrif.cpp:1720`) | `packet_len_table[0x3d]` (`chrif.cpp:42`) + `switch` | `0x2af8`–`0x2b34` |
| map ← char (inter) | `intif_parse` (`intif.cpp:3764`) | `packet_len_table[]` (`intif.cpp:37`) + `switch`; returns 0 = unknown, 1 = handled, 2 = need more data | `0x3800`+ |
| char ← map | `chmapif_parse` (`char_mapif.cpp:1398`) | `switch` on `0x2afa`… ; the `default:` case calls `inter_parse_frommap` (`char_mapif.cpp:1450` → `inter.cpp:1408`), which uses `inter_recv_packet_length[]` | `0x2af8`+ / `0x3000`+ |
| char ← login | `chlogif_parse` (`char_logif.cpp:750`) | `switch` | `0x2711`–`0x2743` |
| login ← char | `logchrif_parse` (`loginchrif.cpp:776`) | `switch` | `0x2712`–`0x2742` |

Request/response pairs on the inter link follow a numeric convention: map→char `0x30xx` is answered by char→map `0x38xx` (✅ e.g. party create `0x3020` → `mapif_parse_CreateParty` `int_party.cpp:814`, reply `0x3820` → `intif_parse_PartyCreated` `intif.cpp:3801`). `chrif` pairs are less regular (`0x2b26` auth request → `0x2afd` ok / `0x2b27` fail; `0x2b01` save → `0x2b21` ack).

## 5. Obfuscation and shuffling

✅ With `PACKET_OBFUSCATION` (map-server only):

- `clif_cryptKey[3]` is set from `clif_obfuscation.hpp` for the build's `PACKETVER`, or from `PACKET_OBFUSCATION_KEY1..3` if those are defined.
- A new connection's first packet ID is XOR-decoded with `((k0*k1 + k2) >> 16) & 0x7FFF` (`clif.cpp:25713`).
- On `WantToConnection` the session key is initialised (`clif.cpp:10723`). After each packet the key advances: `key = key*k1 + k2` (`clif.cpp:25775`). The ID is decoded with `(key >> 16) & 0x7FFF`.
- `PACKET_OBFUSCATION_WARN` detects a client/server mismatch and logs a hint (`clif.cpp:25726-25740`).
- ✅ **For clients after 2018-03-07 the keys are `0,0,0`** (`clif_obfuscation.hpp`, last `#elif`), so the XOR is a no-op. For this fork's `PACKETVER 20220406`, obfuscation is compiled in but **effectively disabled**. The start-up log line "Packet Obfuscation: Enabled. Keys: 0x00000000…" is expected.

**Shuffle** (`clif_shuffle.hpp`): for specific client dates, the official client remaps the IDs of common action packets (walk, attack, use skill, item actions…). The file redefines those entries; its last block covers `PACKETVER_RE_NUM >= 20211103 || PACKETVER_MAIN_NUM >= 20220330` (the version that applies here ⚠️).

## 6. Outbound: building and sending map packets

```cpp
// Modern (preferred) — clif.cpp:~2152 clif_changemap
PACKET_ZC_NPCACK_MAPMOVE packet{};
packet.packetType = HEADER_ZC_NPCACK_MAPMOVE;
mapindex_getmapname_ext(map_mapid2mapname(m), packet.mapName);
packet.xPos = x; packet.yPos = y;
clif_send( &packet, sizeof( packet ), &sd, SELF );
```

`clif_send(buf, len, bl, send_target)` (`clif.cpp:477`) fans a packet out based on `send_target` (✅ all cases handled in the switch):

| Group | Targets |
|---|---|
| Global | `ALL_CLIENT`, `ALL_SAMEMAP` |
| Area (visible range around `bl`) | `AREA`, `AREA_WOS` (without self), `AREA_WOC` (without chatroom members), `AREA_WOSC`, `AREA_CHAT_WOC` |
| Chat room | `CHAT`, `CHAT_WOS` |
| Party | `PARTY`, `PARTY_WOS`, `PARTY_SAMEMAP(_WOS)`, `PARTY_AREA(_WOS)` |
| Guild | `GUILD`, `GUILD_WOS`, `GUILD_SAMEMAP(_WOS)`, `GUILD_AREA(_WOS)`, `GUILD_NOBG` |
| Battleground | `BG`, `BG_WOS`, `BG_SAMEMAP(_WOS)`, `BG_AREA(_WOS)` |
| Duel | `DUEL`, `DUEL_WOS` |
| Single | `SELF` |

Area sends use `map_foreachinallarea` over `AREA_SIZE` (= `battle_config.area_size`, `map.hpp`). For a single known recipient, `socket_send(fd, packet)` / `clif_send(..., SELF)` are equivalent.

## 7. How to add a packet

### New client → map packet (modern style)

1. Define the struct and header in `src/map/packets.hpp`, guarded by the client version range that supports it:
   ```cpp
   #if PACKETVER_MAIN_NUM >= 20220101
   struct PACKET_CZ_MY_REQUEST { int16 packetType; uint32 value; } __attribute__((packed));
   DEFINE_PACKET_HEADER(CZ_MY_REQUEST, 0x0bXX);
   #endif
   ```
2. Register it in `src/map/clif_packetdb.hpp` inside a matching `#if`:
   `parseable_packet( HEADER_CZ_MY_REQUEST, sizeof( PACKET_CZ_MY_REQUEST ), clif_parse_MyRequest, 0 );`
3. Implement `void clif_parse_MyRequest(int32 fd, map_session_data* sd)` in `clif.cpp` and declare it in `clif.hpp`. Cast `RFIFOP(fd,0)` to the struct. **Validate everything**: indexes, amounts, state (`pc_isdead`, `sd->state.trading`, …).
4. The ID must fall within `MIN_PACKET_DB` (0x064) and `MAX_PACKET_DB` (0xCFF) (`clif.hpp:61-62`). Anything outside that range is rejected by `clif_parse` and by `packetdb_addpacket`.

### New map → client packet

Define the `ZC_` struct and header the same way, fill it, and send it with `clif_send` or `socket_send`. Keep the function in `clif.cpp`, named `clif_<something>`, so all wire formatting stays in one place.

### New inter-server packet

1. Choose an unused ID in the right range. Map→char inter requests go in `0x30xx`, and the reply in `0x38xx`.
2. Map side: send it from `intif_*`. Add the reply length to `intif.cpp`'s `packet_len_table` and a `case` in `intif_parse`.
3. Char side: add the length to `inter_recv_packet_length[]` and a `case` in `inter_parse_frommap` (or in the matching `int_*` module's parse function).
4. **Rebuild and deploy every server together.** The length tables must agree on both ends.

## 8. Pitfalls

- A `WFIFOSET` with the wrong length, or a missing `packet_db` length, produces "zero-length packet" warnings or desyncs the client.
- Fields read via `pos[]` silently read garbage if a newer client moved them and `clif_packetdb.hpp` wasn't updated.
- Don't trust client data. Handlers are a frequent source of exploits: check bounds before indexing `inventory`, `cart`, `storage`, skill ids, and NPC ids.
- Packing: always use `#pragma pack(push,1)` and `__attribute__((packed))`, as the existing structs do.
