#!/usr/bin/env python3
import subprocess
import time
import os
import signal
import sys

def test_new_game():
    """Test the new game creation and world visibility"""
    print("Starting VERSE client test...")

    # Start the game process
    try:
        proc = subprocess.Popen(
            ['./verse-client'],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            cwd='/Users/eric/verse'
        )

        print("Game started. Collecting output for 10 seconds...")

        # Collect output for 10 seconds
        output_lines = []
        start_time = time.time()

        while time.time() - start_time < 10:
            try:
                line = proc.stdout.readline()
                if line:
                    output_lines.append(line.strip())
                    print(f"LOG: {line.strip()}")
                elif proc.poll() is not None:
                    # Process ended
                    break
                time.sleep(0.1)
            except:
                break

        # Terminate the process
        proc.terminate()
        proc.wait(timeout=5)

        print("\n=== ANALYSIS ===")
        print(f"Collected {len(output_lines)} log lines")

        # Look for key indicators
        spawn_lines = [line for line in output_lines if 'spawn' in line.lower()]
        error_lines = [line for line in output_lines if 'failed' in line.lower() or 'error' in line.lower()]
        world_lines = [line for line in output_lines if 'world' in line.lower()]

        print(f"\nSpawn-related lines ({len(spawn_lines)}):")
        for line in spawn_lines:
            print(f"  {line}")

        print(f"\nError lines ({len(error_lines)}):")
        for line in error_lines:
            print(f"  {line}")

        print(f"\nWorld-related lines ({len(world_lines)}):")
        for line in world_lines[:5]:  # Show first 5 only
            print(f"  {line}")

        return len(error_lines) == 0

    except Exception as e:
        print(f"Error during test: {e}")
        return False

if __name__ == "__main__":
    success = test_new_game()
    print(f"\nTest {'PASSED' if success else 'FAILED'}")
    sys.exit(0 if success else 1)
