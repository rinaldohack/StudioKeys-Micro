# host/ — StudioKEYS native host

The firmware’s C compiled for the host process. Same method as `web/emu/felucca_web.c`
(reference only). The plugin under `plugin/StudioKEYS/` talks to this library through
`sk_host.h` (input queue, 44.1 kHz render, 240×240 framebuffer, 1 MiB NOR).

```
cc -O2 -ffp-contract=off -w -DNVOICE=32 \
   -I../build/gen -I../firmware/src -I. \
   -c sk_host.c -o sk_host.o
```

Exit check (plan phase 1): heavy song sample-match vs wasm via `web/emu/native_check.c`.

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
| `07` | EVENT | param id (2×7-bit) + value (2×7-bit, offset +8192) |
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

1. Plugin sends **HELLO** with its build string (`FELUCCA_VERSION`).
2. Host/keyboard answers **HELLO_ACK** (its version) and **CRC_ACK** (NOR / state CRC).
3. Version mismatch → RESULT refuse; UI stays unlocked.
4. Connect dialog asks VST→KB or KB→VST. Plugin sends **CRC** then **CHOOSE**.
5. Host rejects **CHOOSE** until the peer CRC matches the local snapshot CRC.
6. On success → lock ON; **EVENT** ticks both ways; `sk_host_lock_autosave` keeps NOR marked.
7. **DROP** / unplug ends the lock.

In-process (plugin + `sk_host.c` in one DAW process): `sk_host_link_push` parses SKL
immediately and queues replies for `sk_host_link_pop`. Same NOR image ⇒ CRCs match when
versions match. On-device firmware answers HELLO with build version + CRC when cable-3
SysEx hits the SKL header (see `firmware/src/sk_link_lock.c`).
