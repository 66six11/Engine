"""Bounded process runner for registration-only Host verification."""

from __future__ import annotations

import subprocess
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


PROCESS_READ_CHUNK_SIZE = 64 * 1024
MAX_VERIFICATION_STDERR_BYTES = 1024 * 1024


@dataclass(frozen=True)
class BoundedHostProcessResult:
    return_code: int
    stdout: bytes = field(repr=False)
    stderr: bytes = field(repr=False)
    stdout_size: int
    stderr_size: int
    limit_exceeded: str | None


class HostProcessTimeout(Exception):
    pass


class HostProcessSpawnFailure(Exception):
    pass


@dataclass
class _Capture:
    limit: int
    chunks: list[bytes] = field(default_factory=list)
    size: int = 0
    retained_size: int = 0
    exceeded: bool = False
    error: OSError | ValueError | None = None

    def read(self, stream: Any, overflow: threading.Event) -> None:
        try:
            while True:
                chunk = stream.read(PROCESS_READ_CHUNK_SIZE)
                if not chunk:
                    break
                self.size += len(chunk)
                remaining = self.limit + 1 - self.retained_size
                if remaining > 0:
                    retained = chunk[:remaining]
                    self.chunks.append(retained)
                    self.retained_size += len(retained)
                if self.size > self.limit:
                    self.exceeded = True
                    overflow.set()
                    break
        except (OSError, ValueError) as error:
            self.error = error
            overflow.set()
        finally:
            try:
                stream.close()
            except OSError:
                pass

    def content(self) -> bytes:
        return b"".join(self.chunks)


def _terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        process.kill()
    except OSError:
        pass


def run_bounded_host_process(
    arguments: tuple[str, ...],
    environment: dict[str, str],
    timeout_seconds: float,
    max_stdout_bytes: int,
    max_stderr_bytes: int = MAX_VERIFICATION_STDERR_BYTES,
) -> BoundedHostProcessResult:
    """Run with typed argv, isolated cwd, and live stdout/stderr limits."""

    with tempfile.TemporaryDirectory(prefix="asharia-host-verification-") as scratch:
        try:
            process = subprocess.Popen(
                arguments,
                cwd=Path(scratch),
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                shell=False,
            )
        except OSError as error:
            raise HostProcessSpawnFailure from error

        assert process.stdout is not None
        assert process.stderr is not None
        overflow = threading.Event()
        stdout = _Capture(max_stdout_bytes)
        stderr = _Capture(max_stderr_bytes)
        readers = (
            threading.Thread(
                target=stdout.read,
                args=(process.stdout, overflow),
                daemon=True,
            ),
            threading.Thread(
                target=stderr.read,
                args=(process.stderr, overflow),
                daemon=True,
            ),
        )
        for reader in readers:
            reader.start()

        deadline = time.monotonic() + timeout_seconds
        timed_out = False
        while process.poll() is None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                timed_out = True
                _terminate(process)
                break
            if overflow.wait(min(remaining, 0.05)):
                _terminate(process)
                break
        try:
            return_code = process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            _terminate(process)
            return_code = process.wait()
        for reader in readers:
            reader.join(timeout=5.0)

        if timed_out:
            raise HostProcessTimeout
        if (
            any(reader.is_alive() for reader in readers)
            or stdout.error is not None
            or stderr.error is not None
        ):
            raise HostProcessSpawnFailure
        exceeded = "stdout" if stdout.exceeded else "stderr" if stderr.exceeded else None
        return BoundedHostProcessResult(
            return_code,
            stdout.content(),
            stderr.content(),
            stdout.size,
            stderr.size,
            exceeded,
        )


__all__ = [
    "BoundedHostProcessResult",
    "HostProcessSpawnFailure",
    "HostProcessTimeout",
    "MAX_VERIFICATION_STDERR_BYTES",
    "run_bounded_host_process",
]
