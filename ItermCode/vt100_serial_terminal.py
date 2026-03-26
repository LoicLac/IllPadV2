#!/usr/bin/env python3
"""
VT100 Serial Terminal — ILLPAD48 Setup & Runtime Monitor
Requires: pip install pyserial

Optimised for iTerm2 on macOS:
  - Sends arrow keys as atomic ESC sequences (no byte splitting)
  - DEC 2026 synchronized output for tear-free rendering
  - Auto-resizes window to fit setup UI (CSI 8 t)
  - macOS Terminal.app works but lacks DEC 2026 (may show tearing)

Controls:
  Arrow keys   navigation in setup tools
  Ctrl-]       clean exit
  Ctrl-C       clean exit (does NOT send ^C to device)
"""

import sys
import os
import select
import termios
import tty
import time
import signal
import threading
import serial
import serial.tools.list_ports

# ── Configuration ─────────────────────────────────────────────────────────────
BAUDRATE         = 115200       # Ignored by USB CDC, but pyserial requires it
EXIT_KEY         = b"\x1d"      # Ctrl-]
READ_SIZE        = 4096
RECONNECT_S      = 2.0
BOOT_FLUSH_S     = 0.5          # Seconds to discard ESP32 boot garbage
CONFIG_FILE      = os.path.expanduser("~/.illpad_terminal")

# Target window size — fits ILLPAD48 setup UI (grid 72 cols, ~28 rows + margin)
TARGET_COLS      = 82
TARGET_ROWS      = 34

# I/O polling interval — lower = snappier input, higher = less CPU
SELECT_TIMEOUT_S = 0.02         # 20ms — good balance for interactive terminal
# ─────────────────────────────────────────────────────────────────────────────

# VT100 sequences
VT_CLEAR_HOME    = "\x1b[2J\x1b[H"
VT_HIDE_CURSOR   = "\x1b[?25l"
VT_SHOW_CURSOR   = "\x1b[?25h"
VT_RESET_ATTRS   = "\x1b[0m"
VT_RESIZE        = "\x1b[8;{rows};{cols}t"


# ── Port persistence ──────────────────────────────────────────────────────────

def load_saved_port():
    try:
        with open(CONFIG_FILE) as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def save_port(port_name):
    try:
        with open(CONFIG_FILE, "w") as f:
            f.write(port_name)
    except OSError:
        pass


# ── Port selection ────────────────────────────────────────────────────────────

def choose_port():
    saved = load_saved_port()

    while True:
        ports = list(serial.tools.list_ports.comports())

        if len(ports) == 1:
            print(f"Auto-selecting only port: {ports[0].device}")
            save_port(ports[0].device)
            return ports[0].device

        available = [p.device for p in ports]
        if saved and saved in available:
            print(f"Using last port: {saved}  (Ctrl-C to pick another)")
            time.sleep(1.0)
            return saved

        print("\nAvailable serial ports:")
        if ports:
            for i, p in enumerate(ports):
                print(f"  [{i}] {p.device}  —  {p.description}")
        else:
            print("  (none found)")
        print("  [r] rescan")
        print("  [q] quit")

        choice = input("Select port index / r / q: ").strip().lower()
        if choice in ("q", "quit"):
            sys.exit(0)
        if choice in ("r", "rescan"):
            continue
        if choice.isdigit() and 0 <= int(choice) < len(ports):
            port_name = ports[int(choice)].device
            save_port(port_name)
            return port_name
        print("Invalid selection, try again.")


# ── Terminal raw mode ─────────────────────────────────────────────────────────

def set_raw_mode(fd):
    """Every keypress sent immediately, no local echo."""
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    return old


def restore_mode(fd, old):
    termios.tcsetattr(fd, termios.TCSADRAIN, old)


# ── iTerm2 window resize ──────────────────────────────────────────────────────

def get_terminal_size():
    try:
        sz = os.get_terminal_size()
        return sz.lines, sz.columns
    except OSError:
        return None, None


def resize_terminal(stdout, rows, cols):
    """Send iTerm2/xterm XTWINOPS resize sequence."""
    stdout.write(VT_RESIZE.format(rows=rows, cols=cols))
    stdout.flush()
    time.sleep(0.15)


def check_and_resize(stdout):
    orig_rows, orig_cols = get_terminal_size()
    if orig_rows is None:
        return None, None

    if orig_cols < TARGET_COLS or orig_rows < TARGET_ROWS:
        resize_terminal(stdout, TARGET_ROWS, TARGET_COLS)
        new_rows, new_cols = get_terminal_size()
        if new_cols and new_cols < TARGET_COLS:
            stdout.write(
                f"\r\n\x1b[33mWarning: terminal is {new_cols}x{new_rows}, "
                f"need {TARGET_COLS}x{TARGET_ROWS}. Display may wrap.\x1b[0m\r\n"
            )
            stdout.flush()
            time.sleep(1.5)

    return orig_rows, orig_cols


# ── Boot garbage filter + frame sync ─────────────────────────────────────────

_FRAME_START = b"\x1b[H"
FRAME_SYNC_TIMEOUT_S = 6.0


def flush_boot_garbage(ser, stdout_buf):
    """
    Phase 1 — discard data for BOOT_FLUSH_S seconds (ESP32 boot log).
    Phase 2 — buffer until ESC[H (start of a real frame), then emit clean.
    """
    sys.stdout.write("\x1b[2J\x1b[H\x1b[2m[Waiting for device...]\x1b[0m")
    sys.stdout.flush()

    # Phase 1: hard discard
    deadline = time.monotonic() + BOOT_FLUSH_S
    while time.monotonic() < deadline:
        try:
            rlist, _, _ = select.select([ser], [], [], 0.05)
            if rlist:
                ser.read(READ_SIZE)
        except (serial.SerialException, ValueError):
            return

    # Phase 2: accumulate until ESC[H found
    buf = b""
    deadline = time.monotonic() + FRAME_SYNC_TIMEOUT_S
    synced = False

    while time.monotonic() < deadline:
        try:
            rlist, _, _ = select.select([ser], [], [], 0.05)
        except (serial.SerialException, ValueError):
            break
        if not rlist:
            continue
        try:
            chunk = ser.read(READ_SIZE)
        except serial.SerialException:
            break
        if not chunk:
            continue

        buf += chunk
        idx = buf.find(_FRAME_START)
        if idx != -1:
            sys.stdout.write("\x1b[2J")
            sys.stdout.flush()
            frame = buf[idx:]
            frame = _normalize_line_endings(frame)
            stdout_buf.write(frame)
            stdout_buf.flush()
            synced = True
            break

        if len(buf) > READ_SIZE * 2:
            buf = buf[-(len(_FRAME_START) - 1):]

    if not synced:
        sys.stdout.write("\x1b[2J\x1b[H")
        sys.stdout.flush()
        if buf:
            fallback = _normalize_line_endings(buf)
            stdout_buf.write(fallback)
            stdout_buf.flush()


# ── Line ending normalization ────────────────────────────────────────────────

def _normalize_line_endings(data):
    """
    ESP32 Serial.printf sends bare \\n. In raw terminal mode, bare \\n causes
    staircase output (cursor goes down without returning to column 0).
    Convert bare \\n to \\r\\n while preserving existing \\r\\n pairs.
    Also strip 0xFF (UART line noise, rare on USB CDC but harmless to filter).
    """
    data = data.replace(b"\xff", b"")
    # Replace \r\n with placeholder, convert bare \n, restore
    data = data.replace(b"\r\n", b"\x00\x01")
    data = data.replace(b"\n", b"\r\n")
    data = data.replace(b"\x00\x01", b"\r\n")
    return data


# ── Keyboard input: atomic escape sequence reading ───────────────────────────

def _read_keyboard_input(stdin_fd):
    """
    Read one keypress from stdin. For escape sequences (arrow keys, etc.),
    drain all immediately-available follow-up bytes and return them together
    so the ESP32 receives the full sequence in one serial write.

    Returns bytes, or None for exit keys (Ctrl-], Ctrl-C).
    """
    ch = sys.stdin.buffer.read(1)
    if not ch:
        return None

    if ch in (EXIT_KEY, b"\x03"):
        return None

    if ch != b"\x1b":
        return ch

    # ESC received — drain follow-up bytes without sleeping.
    # In raw mode, iTerm2 queues the full sequence (ESC [ A) essentially
    # simultaneously. We do two non-blocking reads to catch [ and the letter.
    # No sleep needed — the bytes are already in the tty buffer.
    buf = ch
    for _ in range(8):  # Max 8 extra bytes (covers extended sequences like ESC[1;5C)
        r, _, _ = select.select([stdin_fd], [], [], 0.001)  # 1ms non-blocking
        if not r:
            break
        b = sys.stdin.buffer.read(1)
        if not b:
            break
        buf += b
    return buf


# ── Signal handling ───────────────────────────────────────────────────────────

_exit_event = threading.Event()


def _sigint_handler(signum, frame):
    _exit_event.set()


# ── Main loop ─────────────────────────────────────────────────────────────────

def run(port_name):
    stdout   = sys.stdout
    stdin_fd = sys.stdin.fileno()

    orig_rows, orig_cols = check_and_resize(stdout)

    stdout.write(VT_CLEAR_HOME + VT_HIDE_CURSOR)
    stdout.flush()

    signal.signal(signal.SIGINT, _sigint_handler)

    ser = None
    try:
        ser = serial.Serial(port_name, BAUDRATE, timeout=0)
    except serial.SerialException as e:
        stdout.write(f"\r\nFailed to open port: {e}\r\n")
        stdout.flush()
        sys.exit(1)

    old_stdin = set_raw_mode(stdin_fd)
    stdout_buf = sys.stdout.buffer

    flush_boot_garbage(ser, stdout_buf)

    try:
        while not _exit_event.is_set():

            # ── Reconnect after ESP32 reset / USB unplug ──────────────
            if not ser.is_open:
                stdout.write(
                    "\r\n\x1b[33m[Disconnected — reconnecting...]\x1b[0m\r\n"
                )
                stdout.flush()
                while not _exit_event.is_set():
                    time.sleep(RECONNECT_S)
                    try:
                        ser.open()
                        flush_boot_garbage(ser, stdout_buf)
                        break
                    except serial.SerialException:
                        pass
                continue

            # ── I/O multiplexing ──────────────────────────────────────
            try:
                rlist, _, _ = select.select(
                    [stdin_fd, ser], [], [], SELECT_TIMEOUT_S
                )
            except (ValueError, select.error):
                ser.close()
                continue

            # ── Serial → terminal ─────────────────────────────────────
            if ser in rlist:
                try:
                    data = ser.read(READ_SIZE)
                except serial.SerialException:
                    ser.close()
                    continue

                if data:
                    data = _normalize_line_endings(data)
                    stdout_buf.write(data)
                    stdout_buf.flush()

            # ── Keyboard → serial ─────────────────────────────────────
            if stdin_fd in rlist:
                keys = _read_keyboard_input(stdin_fd)
                if keys is None:
                    break
                try:
                    ser.write(keys)
                except serial.SerialException:
                    ser.close()

    finally:
        restore_mode(stdin_fd, old_stdin)
        signal.signal(signal.SIGINT, signal.SIG_DFL)

        stdout.write(VT_SHOW_CURSOR + VT_RESET_ATTRS + "\r\n")

        if orig_rows and orig_cols:
            resize_terminal(stdout, orig_rows, orig_cols)

        stdout.flush()
        if ser and ser.is_open:
            ser.close()
        print("Serial port closed.")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    port_name = choose_port()
    print(f"\nOpening {port_name} at {BAUDRATE} baud")
    print("Ctrl-]  or  Ctrl-C  to exit\n")
    run(port_name)


if __name__ == "__main__":
    main()
