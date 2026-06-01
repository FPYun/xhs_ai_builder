#!/usr/bin/env python
"""Upload sd_card_payload audio clips to CoreS3 SD card over serial SDPUT."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - host environment guard
    raise SystemExit("pyserial is required; use the same Python used for serial.tools.list_ports") from exc


MAX_AUDIO_BYTES = 32768


def read_line(port: serial.Serial, deadline: float) -> str | None:
    buf = bytearray()
    while time.monotonic() < deadline:
        b = port.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode("utf-8", errors="replace").strip()
        if b != b"\r":
            buf.extend(b)
    if buf:
        return buf.decode("utf-8", errors="replace").strip()
    return None


def drain(port: serial.Serial, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        line = read_line(port, min(end, time.monotonic() + 0.2))
        if line:
            print(line)


def wait_for(port: serial.Serial, timeout: float, *needles: str) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = read_line(port, deadline)
        if not line:
            continue
        print(line)
        if any(needle in line for needle in needles):
            return line
    raise TimeoutError("timeout waiting for: " + " | ".join(needles))


def payload_files(payload: Path) -> list[Path]:
    audio_root = payload / "audio"
    if not audio_root.exists():
        raise SystemExit(f"missing audio directory: {audio_root}")
    files = sorted(path for path in audio_root.rglob("*.raw") if path.is_file())
    if not files:
        raise SystemExit(f"no .raw files found under {audio_root}")
    return files


def sd_path(payload: Path, local_path: Path) -> str:
    rel = local_path.relative_to(payload).as_posix()
    return "/" + rel


def upload_file(port: serial.Serial, payload: Path, local_path: Path, timeout: float) -> None:
    data = local_path.read_bytes()
    if not 0 < len(data) <= MAX_AUDIO_BYTES:
        raise ValueError(f"{local_path} size must be 1..{MAX_AUDIO_BYTES} bytes")
    target = sd_path(payload, local_path)
    print(f"upload {target} bytes={len(data)}")
    port.write(f"SDPUT {target} {len(data)}\n".encode("ascii"))
    port.flush()
    wait_for(port, timeout, "serial ready: SDPUT")
    port.write(data)
    port.flush()
    line = wait_for(port, timeout, "serial ok: SDPUT", "serial error:")
    if "serial ok: SDPUT" not in line:
        raise RuntimeError(line)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port, for example COM7")
    parser.add_argument("--payload", default="sd_card_payload", help="Payload folder")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--skip-sdinfo", action="store_true")
    args = parser.parse_args()

    payload = Path(args.payload).resolve()
    files = payload_files(payload)
    with serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=args.timeout) as port:
        port.dtr = False
        port.rts = False
        time.sleep(1.5)
        drain(port, 0.8)
        for local_path in files:
            upload_file(port, payload, local_path, args.timeout)
        if not args.skip_sdinfo:
            print("request SDINFO")
            port.write(b"SDINFO\n")
            port.flush()
            drain(port, 5.0)
    print(f"uploaded {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
