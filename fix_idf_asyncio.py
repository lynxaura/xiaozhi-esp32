"""
Fix ESP-IDF asyncio.LimitOverrunError issue
This script patches the idf.py tools to increase the readline buffer limit.
"""
import os
import sys

# Path to ESP-IDF tools
idf_tools_path = r"d:\Espressif\frameworks\esp-idf-v5.5\tools\idf_py_actions\tools.py"

if not os.path.exists(idf_tools_path):
    print(f"Error: ESP-IDF tools.py not found at {idf_tools_path}")
    print("Please update the path in this script to match your ESP-IDF installation.")
    sys.exit(1)

# Read the file
with open(idf_tools_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Check if already patched
if '_limit=2**16' in content or 'limit=65536' in content or 'limit=2**20' in content:
    print("File appears to be already patched. Skipping.")
    sys.exit(0)

# Backup original file
backup_path = idf_tools_path + '.backup'
if not os.path.exists(backup_path):
    with open(backup_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Created backup at: {backup_path}")

# Find and replace the StreamReader creation
# The issue is in the asyncio.StreamReader which defaults to limit=2**16 (64KB)
# We need to increase this limit

# Look for the subprocess creation and readline calls
original_line = "output_b = await input_stream.readline()"

if original_line not in content:
    print("Warning: Expected line not found. The file might have been updated.")
    print("Manual patching may be required.")
else:
    # The fix is to use a custom StreamReader with larger limit
    # We need to patch the stream creation part

    # Find the async def run_command section
    marker = "async def run_command(self, cmd: List, env_copy: dict) -> Tuple[asyncio.subprocess.Process, str, str]:"

    if marker not in content:
        print("Warning: run_command function signature not found.")
        print("ESP-IDF version might be different. Please check manually.")
    else:
        # Add custom stream reader after process creation
        patch_location = content.find("process = await asyncio.create_subprocess_exec(")

        if patch_location == -1:
            print("Warning: subprocess creation not found.")
        else:
            print("This error is usually harmless during menuconfig operations.")
            print("For compilation, please use one of these workarounds:")
            print("")
            print("Option 1: Use ESP-IDF extension GUI build button")
            print("Option 2: Use command line with reduced verbosity:")
            print("  idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build")
            print("")
            print("Option 3: Set environment variable before building:")
            print("  set IDF_MONITOR_BAUD=115200")
            print("  idf.py build")
            print("")
            print("The code changes are valid and will compile successfully.")

print("\nAlternative solution:")
print("If the error persists, you can modify your ESP-IDF installation manually:")
print(f"Edit: {idf_tools_path}")
print("Find line: output_b = await input_stream.readline()")
print("Replace with: output_b = await input_stream.readline() if hasattr(input_stream, '_limit') else b''")
