# host/ — StudioKEYS native host

The firmware’s C compiled for the host process. The plugin under `plugin/StudioKEYS/`
talks to this library through `sk_host.h` (input queue, 44.1 kHz render, 240×240
framebuffer, 1 MiB NOR). The device bring-up lives in `studiokeys_host.c`.

```
cc -O2 -ffp-contract=off -w -DVOICE_POOL=32 \
   -I../build/gen -I../firmware/src -I. \
   -c sk_host.c -o sk_host.o
```

Exit check (plan phase 1): heavy song native render via `host/native_check.c`.

## Link-port lock SysEx (cable 3 / SKL)

Four MIDI ports; cable 3 is the plugin↔keyboard link (never notes). Lock frames use
manufacturer id `7D` and ASCII `SKL`:

```
F0  7D  53  4B  4C  cmd  [payload…]  F7
     │   S   K   L
```

| cmd | name | payload |
|-----|------|---------|
| `01` | HELLO | ASCII version, NUL-terminated (7-bit) |
| `02` | HELLO_ACK | peer version, same encoding |
| `03` | CRC | snapshot CRC32 as five 7-bit bytes, LSB first |
| `04` | CRC_ACK | peer snapshot CRC32, same packing |
| `05` | CHOOSE | `00` = keyboard → plugin, `01` = plugin → keyboard |
| `06` | RESULT | `00` ok, `01` version refuse, `02` CRC refuse, `03` bad state |
| `07` | EVENT | param id (2×7-bit) + value (2×7-bit, offset +8192). Track params use `id < P_COUNT`. Meta: `0x3FFE` = COLOR palette index, `0x3FFD` = LANG (0 EN / 1 ID) |
| `08` | DROP | (empty) end lock |
| `09` | DISMISS | (empty) close sync prompt without locking |

CRC packing (32-bit → 5 MIDI data bytes):

```
b0 = crc & 0x7F
b1 = (crc >> 7) & 0x7F
b2 = (crc >> 14) & 0x7F
b3 = (crc >> 21) & 0x7F
b4 = (crc >> 28) & 0x0F
```

### Handshake

1. Plugin sends **HELLO** with its build string (`SKM_VERSION`).
2. Host/keyboard answers **HELLO_ACK** (its version) and **CRC_ACK** (NOR / state CRC).
3. Version mismatch → RESULT refuse; UI stays unlocked.
4. Connect dialog asks VST→KB or KB→VST. Plugin sends **CRC** then **CHOOSE**.
5. **CHOOSE** applies a directional **project snapshot** (`project_save` / `project_load`
   slot 0). When CRCs still disagree over USB, the plugin first sends **SNAP_BEGIN /
   SNAP_CHUNK / SNAP_END** (`0x0A`…`0x0C`) with the packed `project_store_t`.
6. On success → lock ON; **EVENT** ticks both ways; `sk_host_lock_autosave` calls
   `project_save(0)` whenever NOR CRC changes. While locked, `sk_lock_plugin_clock`
   stops the keyboard sequencer from free-running.
7. **DROP** / unplug ends the lock.

In-process (plugin + `sk_host.c` in one DAW process): `sk_host_link_push` parses SKL
immediately and queues replies for `sk_host_link_pop`. CHOOSE writes the shared NOR
project slot. On-device firmware handles the same cmds in `firmware/src/sk_link_lock.c`.

## Surface SysEx on the link (cable 3 / SKS)

Plugin Live/HUI controls also ride cable 3 as `SKS` (`host/sk_sks.h`):

```
F0  7D  53  4B  53  cmd  a  b  F7
     │   S   K   S
```

| cmd | name | a, b |
|-----|------|------|
| `01` | LIVE_NOTE | note, velocity → keyboard forwards on Live cable 1 |
| `02` | LIVE_CC | cc, value → Live cable 1 |
| `11` | HUI_FADER | strip 0..7, value → HUI cable 2 pitch-bend |

`sk_host_link_push` dispatches SKS to `sk_surf_link_sysex` (device: `firmware/src/sk_surf.c`;
host/web: stub in `studiokeys_host.c`). `sk_host_surf_hits()` counts handled frames for
`host/sk_link_loopback`. Plugin mirrors the same bytes on CoreMIDI when Link is open
(`LockSession` / `SurfaceBridge` `SetWire`).

---

StudioKEYS Micro firmware and StudioKEYS companion © 2026 Rinaldo Jonathan (LangitCinta! StageLabs) and Hügelton Instruments. The companion VST3, the SMK-37 surface and its UI are Rinaldo Jonathan's work; the GPL engine lineage is Hügelton Instruments'. See `LICENSING.md`.
