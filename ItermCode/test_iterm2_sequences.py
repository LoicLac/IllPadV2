#!/usr/bin/env python3
"""
Test iTerm2 proprietary escape sequences.
Run this directly in iTerm2 to verify which features work.
"""
import sys
import time

def test(name, seq):
    print(f"Testing: {name}...")
    sys.stdout.write(seq)
    sys.stdout.flush()
    time.sleep(1)
    print(f"  -> Did it work? (look at iTerm2)")

# 1. Tab title
test("Tab title (OSC 0)", "\033]0;ILLPAD48 Setup\007")

# 2. Background color (amber)
test("Background color (OSC 1337 SetColors bg)", "\033]1337;SetColors=bg=1a0e00\007")

# 3. Foreground color (amber text)
test("Foreground color (OSC 1337 SetColors fg)", "\033]1337;SetColors=fg=ffaa33\007")

# 4. Badge
test("Badge (OSC 1337 SetBadgeFormat)", "\033]1337;SetBadgeFormat=SUxMUEFENDg=\007")

# 5. Window resize
test("Resize to 120x50 (CSI 8;50;120 t)", "\033[8;50;120t")

# 6. Progress bar
test("Progress bar 50% (OSC 9;4;1;50)", "\033]9;4;1;50\007")
time.sleep(2)
sys.stdout.write("\033]9;4;0;\007")  # clear
sys.stdout.flush()
print("  -> Progress bar cleared")

print("\n--- Test complete ---")
print("If nothing worked, check iTerm2 > Settings > Profiles > Terminal")
print("  - 'Terminal may set tab/window title' should be ON")
print("  - 'Disable session-initiated window resizing' should be OFF")
print("\nPress Ctrl-C to exit. Background will stay amber until you reset profile.")

# Reset hint
print("To reset colors: printf '\\033]1337;SetColors=bg=default\\007\\033]1337;SetColors=fg=default\\007'")

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    # Reset colors on exit
    sys.stdout.write("\033]1337;SetColors=bg=default\007")
    sys.stdout.write("\033]1337;SetColors=fg=default\007")
    sys.stdout.write("\033]0;\007")  # clear title
    sys.stdout.flush()
    print("\nColors and title reset.")
