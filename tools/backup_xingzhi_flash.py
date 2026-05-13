#!/usr/bin/env python3
"""Back up the current Xiaozhi flash image from a Xingzhi ESP32-S3 board."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Callable, Sequence, TextIO


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BACKUP_DIR = REPO_ROOT / "backups"
DEFAULT_PORT = "COM7"
DEFAULT_CHIP = "esp32s3"
DEFAULT_BAUD = 460800


@dataclass(frozen=True)
class CommandResult:
    args: Sequence[str]
    returncode: int
    stdout: str
    stderr: str


Runner = Callable[[Sequence[str]], CommandResult]


def run_command(args: Sequence[str]) -> CommandResult:
    completed = subprocess.run(args, capture_output=True, text=True, check=False)
    return CommandResult(args, completed.returncode, completed.stdout, completed.stderr)


def parse_size(value: str) -> int:
    text = value.strip().upper().replace(" ", "")
    if text.startswith("0X"):
        return int(text, 16)

    match = re.fullmatch(r"(\d+)([KMG]?B?)?", text)
    if not match:
        raise ValueError(f"Unsupported flash size: {value!r}")

    amount = int(match.group(1))
    suffix = match.group(2) or ""
    if suffix in ("K", "KB"):
        return amount * 1024
    if suffix in ("M", "MB"):
        return amount * 1024 * 1024
    if suffix in ("G", "GB"):
        return amount * 1024 * 1024 * 1024
    return amount


def parse_detected_flash_size(output: str) -> int:
    patterns = [
        r"Detected flash size:\s*([0-9]+\s*[KMG]?B|0x[0-9a-fA-F]+)",
        r"Flash size:\s*([0-9]+\s*[KMG]?B|0x[0-9a-fA-F]+)",
    ]
    for pattern in patterns:
        match = re.search(pattern, output, re.IGNORECASE)
        if match:
            return parse_size(match.group(1))
    raise ValueError("Could not find flash size in esptool output")


def esptool_command(port: str, chip: str, baud: int, no_stub: bool = False) -> list[str]:
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "--port",
        port,
        "--baud",
        str(baud),
    ]
    if no_stub:
        command.append("--no-stub")
    return command


def detect_flash_size(port: str, chip: str, baud: int, no_stub: bool, runner: Runner) -> int:
    result = runner([*esptool_command(port, chip, baud, no_stub), "flash-id"])
    if result.returncode != 0:
        raise RuntimeError(
            "esptool flash_id failed:\n"
            f"{result.stdout.strip()}\n{result.stderr.strip()}".strip()
        )
    return parse_detected_flash_size(result.stdout + "\n" + result.stderr)


def default_output_path(port: str, size: int, now: datetime | None = None) -> Path:
    timestamp = (now or datetime.now()).strftime("%Y%m%d-%H%M%S")
    safe_port = re.sub(r"[^A-Za-z0-9_.-]+", "_", port)
    return DEFAULT_BACKUP_DIR / f"xiaozhi-{safe_port}-{timestamp}-{size:#x}.bin"


def verify_backup(path: Path, expected_size: int) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Backup file was not created: {path}")
    actual_size = path.stat().st_size
    if actual_size != expected_size:
        raise ValueError(
            f"Backup size mismatch: expected {expected_size} bytes, got {actual_size} bytes"
        )


def read_flash(
    port: str,
    chip: str,
    baud: int,
    size: int,
    output: Path,
    runner: Runner,
    no_stub: bool = False,
    chunk_size: int | None = None,
    retries: int = 0,
    min_chunk_size: int = 4096,
    stdout: TextIO = sys.stdout,
) -> CommandResult:
    output.parent.mkdir(parents=True, exist_ok=True)
    if chunk_size and chunk_size < size:
        return read_flash_chunked(
            port,
            chip,
            baud,
            size,
            output,
            runner,
            no_stub,
            chunk_size,
            min_chunk_size,
            retries,
            stdout,
        )

    return read_flash_range(port, chip, baud, 0, size, output, runner, no_stub)


def read_flash_range(
    port: str,
    chip: str,
    baud: int,
    start: int,
    size: int,
    output: Path,
    runner: Runner,
    no_stub: bool,
) -> CommandResult:
    command = [
        *esptool_command(port, chip, baud, no_stub),
        "read-flash",
        hex(start),
        hex(size),
        str(output),
    ]
    return runner(command)


def read_flash_chunked(
    port: str,
    chip: str,
    baud: int,
    size: int,
    output: Path,
    runner: Runner,
    no_stub: bool,
    chunk_size: int,
    min_chunk_size: int,
    retries: int,
    stdout: TextIO,
) -> CommandResult:
    chunk_dir = output.parent / f".{output.name}.chunks"
    chunk_dir.mkdir(parents=True, exist_ok=True)

    offset = 0
    parts: list[tuple[Path, int]] = []
    while offset < size:
        current_size = min(chunk_size, size - offset)
        part = chunk_dir / f"part-{offset:08x}.bin"
        print(f"Reading chunk {offset:#x}+{current_size:#x}", file=stdout, flush=True)
        result = read_part_adaptive(
            port,
            chip,
            baud,
            offset,
            current_size,
            part,
            chunk_dir,
            runner,
            no_stub,
            min_chunk_size,
            retries,
            stdout,
        )
        if result.returncode != 0:
            return result
        parts.append((part, current_size))
        offset += current_size

    with output.open("wb") as combined:
        for part, expected_size in parts:
            verify_backup(part, expected_size)
            combined.write(part.read_bytes())
    shutil.rmtree(chunk_dir, ignore_errors=True)
    return CommandResult([], 0, "", "")


def read_part_adaptive(
    port: str,
    chip: str,
    baud: int,
    offset: int,
    size: int,
    output: Path,
    scratch_dir: Path,
    runner: Runner,
    no_stub: bool,
    min_chunk_size: int,
    retries: int,
    stdout: TextIO,
) -> CommandResult:
    if output.exists():
        try:
            verify_backup(output, size)
            print(f"Reusing cached chunk {offset:#x}+{size:#x}", file=stdout, flush=True)
            return CommandResult([], 0, "", "")
        except Exception:
            output.unlink()

    result = CommandResult([], 1, "", "chunk read was not attempted")
    for attempt in range(retries + 1):
        if output.exists():
            output.unlink()
        if attempt:
            print(f"Retrying chunk {offset:#x}+{size:#x} ({attempt}/{retries})", file=stdout, flush=True)
        result = read_flash_range(port, chip, baud, offset, size, output, runner, no_stub)
        if result.returncode == 0:
            try:
                verify_backup(output, size)
                return result
            except Exception as exc:
                result = CommandResult(result.args, 1, result.stdout, str(exc))

    if size <= min_chunk_size:
        return result

    left_size = size // 2
    left_size -= left_size % 4096
    if left_size <= 0:
        left_size = size // 2
    right_size = size - left_size
    left = scratch_dir / f"part-{offset:08x}-{left_size:x}.bin"
    right = scratch_dir / f"part-{offset + left_size:08x}-{right_size:x}.bin"
    print(
        f"Splitting chunk {offset:#x}+{size:#x} into {left_size:#x} and {right_size:#x}",
        file=stdout,
        flush=True,
    )

    left_result = read_part_adaptive(
        port,
        chip,
        baud,
        offset,
        left_size,
        left,
        scratch_dir,
        runner,
        no_stub,
        min_chunk_size,
        retries,
        stdout,
    )
    if left_result.returncode != 0:
        return left_result

    right_result = read_part_adaptive(
        port,
        chip,
        baud,
        offset + left_size,
        right_size,
        right,
        scratch_dir,
        runner,
        no_stub,
        min_chunk_size,
        retries,
        stdout,
    )
    if right_result.returncode != 0:
        return right_result

    output.write_bytes(left.read_bytes() + right.read_bytes())
    verify_backup(output, size)
    return CommandResult([], 0, "", "")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Read the full flash image from a Xingzhi ESP32-S3 before flashing Clawdmeter."
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port, for example COM7")
    parser.add_argument("--chip", default=DEFAULT_CHIP, help="esptool chip name")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    parser.add_argument(
        "--size",
        help="Flash size override, for example 8MB or 0x800000. Defaults to esptool flash_id.",
    )
    parser.add_argument("--output", type=Path, help="Backup image path")
    parser.add_argument(
        "--chunk-size",
        help="Read flash in smaller chunks, for example 256KB, then merge into one backup file.",
    )
    parser.add_argument(
        "--min-chunk-size",
        default="4KB",
        help="Smallest adaptive split size when a chunk keeps failing.",
    )
    parser.add_argument(
        "--no-stub",
        action="store_true",
        help="Pass --no-stub to esptool if the stub flasher is unstable.",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=2,
        help="Retries per chunk when --chunk-size is used.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the esptool commands without reading flash.",
    )
    return parser


def run(
    argv: Sequence[str] | None = None,
    runner: Runner = run_command,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
) -> int:
    args = build_parser().parse_args(argv)

    try:
        size = parse_size(args.size) if args.size else detect_flash_size(
            args.port, args.chip, args.baud, args.no_stub, runner
        )
        output = args.output or default_output_path(args.port, size)
        chunk_size = parse_size(args.chunk_size) if args.chunk_size else None
        min_chunk_size = parse_size(args.min_chunk_size)

        print(f"Port: {args.port}", file=stdout)
        print(f"Chip: {args.chip}", file=stdout)
        print(f"Flash size: {size} bytes ({size:#x})", file=stdout)
        print(f"Backup path: {output}", file=stdout)
        if chunk_size:
            print(f"Chunk size: {chunk_size} bytes ({chunk_size:#x})", file=stdout)
            print(f"Minimum chunk size: {min_chunk_size} bytes ({min_chunk_size:#x})", file=stdout)

        if args.dry_run:
            print("Dry run: flash was not read.", file=stdout)
            print(
                "Detect command:",
                " ".join(esptool_command(args.port, args.chip, args.baud, args.no_stub) + ["flash-id"]),
                file=stdout,
            )
            print(
                "Read command:",
                " ".join(
                    esptool_command(args.port, args.chip, args.baud, args.no_stub)
                    + ["read-flash", "0x0", hex(size), str(output)]
                ),
                file=stdout,
            )
            if chunk_size and chunk_size < size:
                print("Chunked read will issue one read-flash command per chunk.", file=stdout)
            return 0

        result = read_flash(
            args.port,
            args.chip,
            args.baud,
            size,
            output,
            runner,
            args.no_stub,
            chunk_size,
            max(args.retries, 0),
            min_chunk_size,
            stdout,
        )
        if result.returncode != 0:
            print(result.stdout, file=stdout, end="")
            print(result.stderr, file=stderr, end="")
            if output.exists():
                output.unlink()
                print(f"Removed incomplete backup: {output}", file=stderr)
            print("Flash backup failed.", file=stderr)
            return result.returncode or 1

        try:
            verify_backup(output, size)
        except Exception:
            if output.exists():
                output.unlink()
            raise
        print(f"Backup verified: {output} ({size} bytes)", file=stdout)
        return 0
    except Exception as exc:
        print(f"Error: {exc}", file=stderr)
        return 1


def main() -> int:
    return run()


if __name__ == "__main__":
    raise SystemExit(main())
