# Battle Link Runtime

This document records the current CoreS3-to-CoreS3 battle link behavior and the
next protocol extension proposal. It does not change any public header.

## Constraints

- Do not redefine public types.
- Do not modify `pet_model.h`, `vision_types.h`, `ui_types.h`, or
  `battle_protocol.h` without architecture approval.
- Do not change `BattlePetPacket` fields, field order, or packed layout.
- Keep the firmware local and offline.
- Do not restore voice recognition.
- Do not add dependencies or cloud API keys.

## Current Communication State Machine

```text
BOOT
  -> NET_INIT
      - Read Wi-Fi MAC.
      - COM8 / 44:1B:F6:E3:9A:FC and COM7 / 44:1B:F6:E3:9B:60 are hardware labels only.
      - Start Wi-Fi/UDP on port 42105.
      - Print serial identity: mac, board label, internal role, runtime state, AP SSID, UDP port.

NET_READY
  -> IDLE
      - UI is usable.
      - No peer discovery is performed outside MATCH/BATTLE.

MATCH_ENTER
  -> MATCH_LINKING
      - Reset peer IP, TX/RX/failure counters, and duplicate-result key.
      - Make the first asynchronous scan eligible immediately.
      - Start as internal HOST while scanning asynchronously.
      - If a lower peer ID is discovered, switch to internal CLIENT and connect to that peer AP.
      - If no lower peer ID is discovered, keep waiting as internal HOST.

MATCH_LINKING
  -> MATCH_EXCHANGE
      - CLIENT uses gateway IP as target after STA connects.
      - HOST learns peer IP from the first valid UDP packet.
      - Both sides keep UI responsive during link maintenance.

MATCH_EXCHANGE
  -> BATTLE_PENDING
      - With an active pet, each side sends BattlePetPacket every 450 ms.
      - A valid inbound BattlePetPacket is treated as peer presence and pet data.
      - Duplicate battle results are filtered by device/seq/local pet key.

BATTLE_PENDING
  -> BATTLE_RESULT
      - The clash screen is shown first.
      - The result screen is shown after the configured delay.

MATCH_LINKING or MATCH_EXCHANGE
  -> LINK_TIMEOUT_RECOVERY
      - If no peer packet is received for 12000 ms, clear peer IP and stale peer-seen status.
      - Internal CLIENT retries the selected peer SSID every 5000 ms.
      - Internal HOST keeps scanning/waiting for the next inbound packet.
      - The middle MATCH button can return to IDLE at any time.
```

## MATCH Pairing Flow

The player-facing flow is role-neutral:

1. Both players select an active pet.
2. Both enter MATCH.
3. UI shows neutral link text such as `MATCHING`, `PAIRING`, `CONNECTED`,
   `BATTLING`, or `RETRYING`.
4. Serial logs keep the engineering role detail:
   `role=HOST` or `role=CLIENT`.
5. Each board advertises an `M5PET-xxxxxx` AP derived from its own MAC.
6. A board that sees a lower peer ID connects as internal CLIENT; the lower-ID
   board remains internal HOST.
7. COM7/COM8 labels are only used for flash and serial records, not role choice.
8. Once both active pets exchange valid `BattlePetPacket` payloads, both boards
   enter the battle result flow.
9. Middle button exits MATCH to IDLE.

Dynamic pairing is non-blocking:

- Scanning is asynchronous, so UI input remains responsive.
- The player-facing UI never exposes HOST or CLIENT.

## Heartbeat, ACK, and Timeout Strategy

Current firmware uses the existing pet packet as the only on-wire payload:

- Heartbeat: repeated `BattlePetPacket` send while in MATCH/BATTLE and an active
  pet exists.
- Peer liveness: `last_peer_seen_ms` updates on every valid packet.
- Timeout: after `kBattlePeerTimeoutMs`, peer IP and stale peer-seen status are
  cleared, then reconnect/wait behavior resumes.
- ACK: not implemented in the current wire protocol.

This is enough for the current offline two-board demo because a repeated pet
packet both advertises the active pet and confirms peer liveness. It is not
enough for richer diagnostics because it cannot distinguish:

- Connected but no active pet.
- Packet received but battle result not yet displayed.
- Peer intentionally left MATCH.
- Packet loss vs. application-level rejection.

## Interface Change Suggestion

Do not implement this in firmware until the system architecture module accepts
the interface change.

```text
Interface change request:
- Header: battle_protocol.h
- Current type or constant: BattlePetPacket, kBattleVersion
- Proposed change:
  Add a separate packed BattleControlPacket and BattleControlType enum.
  Do not modify BattlePetPacket.
  Suggested control fields: magic, version, type, deviceId, seq, ackSeq,
  flags, uptimeMs.
- Reason:
  Support explicit HELLO, HEARTBEAT, ACK, LEAVE_MATCH, and ERROR without
  overloading pet payloads.
- Compatibility impact:
  Existing firmware ignores non-pet packets because the UDP length does not
  equal sizeof(BattlePetPacket). New firmware can keep accepting the existing
  pet packet unchanged.
- Required version bump:
  No BattlePetPacket version bump. Add kBattleControlVersion = 1 for the new
  control packet.
- Migration plan:
  Keep BattlePetPacket as the authoritative battle payload. Send control
  packets opportunistically for link state and ACK. Fall back to pet-packet
  heartbeat when the peer does not ACK controls.
- Compile/test command:
  .\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_flash
```

## Local App Entry Recommendation

Phase 1 should use USB serial commands only:

- `BTN 0`, `BTN 1`, `BTN 2` for left/middle/right input.
- `ACT <UiAction>` for debug-only direct action injection.
- `STATUS` for current screen, selected pet, link counters, MAC, and role.
- Current firmware already prints boot identity and MATCH/BATTLE runtime status
  logs for hardware verification.

Phase 2 can add local network HTTP or UDP status/control once the board-to-board
link is stable.

Phase 3 can evaluate BLE only after USB and LAN workflows are proven.

## 2026-05-29 Compile and Flash Record

Compile command:

```powershell
.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_flash
```

Compile result:

```text
Sketch uses 1051377 bytes (33%) of program storage space. Maximum is 3145728 bytes.
Global variables use 64744 bytes (19%) of dynamic memory, leaving 262936 bytes for local variables. Maximum is 327680 bytes.
```

Port detection command:

```powershell
E:\Anaconda\python.exe -c "import serial.tools.list_ports; print('\n'.join(f'{p.device} | {p.description} | {p.hwid}' for p in serial.tools.list_ports.comports()))"
```

Latest observed ports:

```text
COM7 | USB serial device | USB VID:PID=303A:1001 SER=44:1B:F6:E3:9B:60
Arduino CLI board list reported COM7 as Serial Port (USB).
COM8 was not present.
```

Flash record:

| Board label | Expected COM | Expected MAC | Write | Hash verify | Hard reset | Result |
| --- | --- | --- | --- | --- | --- | --- |
| Known board | COM8 | 44:1B:F6:E3:9A:FC | Not run | Not run | Not run | COM8 absent |
| Known board | COM7 | 44:1B:F6:E3:9B:60 | Success | Success | Success | Flashed after adding MATCH/BATTLE serial status logs; boot serial printed `board=COM7 role=HOST state=DISCOVERING ap=M5PET-E39B60` |

Dual-board test result:

- Full dual-board test was not executed because COM8 was not enumerated.
- COM7 single-board boot was verified on serial:
  `battle mac=44:1B:F6:E3:9B:60 board=COM7 role=HOST state=DISCOVERING ap=M5PET-E39B60 udp=42105`.
- Required checks once both boards are connected:
  - Serial identity prints each board MAC and dynamic internal role.
  - MATCH UI remains role-neutral.
  - One board becomes internal CLIENT and reaches link connected state.
  - TX and RX counters change on both boards when both have active pets.
  - A valid peer packet enters BATTLE result flow.
  - Middle button exits MATCH.
