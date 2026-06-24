#!/usr/bin/env python3
import os
import sys
import time
import argparse
from datetime import datetime

# Try importing dependencies and print friendly helper message if they are missing
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: 'pyserial' library is required.")
    print("Please install it by running: pip install pyserial")
    sys.exit(1)

try:
    from PIL import Image
except ImportError:
    print("Error: 'Pillow' library is required.")
    print("Please install it by running: pip install Pillow")
    sys.exit(1)

WIDTH = 80
HEIGHT = 160
EXPECTED_BYTES = WIDTH * HEIGHT * 2  # RGB565 (2 bytes per pixel)

def find_serial_port():
    """Attempts to find the Axiometa / ESP32-S3 serial port automatically."""
    ports = list(serial.tools.list_ports.comports())
    # Common USB to Serial descriptions
    usb_ports = [p.device for p in ports if "usb" in p.device.lower() or "usbmodem" in p.device.lower() or "usbserial" in p.device.lower()]
    if usb_ports:
        return usb_ports[0]
    return None

def list_all_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found. Make sure your device is connected.")
        return
    print("\nAvailable Serial Ports:")
    for p in ports:
        print(f"  - {p.device} ({p.description})")

def main():
    parser = argparse.ArgumentParser(description="Axiometa Serial Screen Dump Utility")
    parser.add_argument("-p", "--port", help="Serial port (e.g. /dev/cu.usbmodem1101)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-o", "--output-dir", default=".", help="Directory to save screenshots (default: current directory)")
    parser.add_argument("--swap-rgb", action="store_true", help="Swap Red and Blue channels if colors look reversed on PC")
    
    args = parser.parse_args()
    
    port = args.port
    if not port:
        port = find_serial_port()
        if not port:
            print("Could not automatically detect a USB serial port.")
            list_all_ports()
            sys.exit(1)
        print(f"Auto-detected serial port: {port}")
        
    output_dir = args.output_dir
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    print(f"Connecting to {port} at {args.baud} baud...")
    try:
        ser = serial.Serial(port, args.baud, timeout=2.0)
    except Exception as e:
        print(f"Error opening serial port {port}: {e}")
        sys.exit(1)
        
    print("\nListening for screen dumps...")
    print("Double-click the button or rotary encoder on your device to trigger a screenshot.\n")
    
    buffer = b""
    capturing = False
    
    try:
        while True:
            line = ser.readline()
            if not line:
                continue
                
            if b"---START_SCREENSHOT---" in line:
                print("Dump detected! Receiving screen data...", end="", flush=True)
                capturing = True
                raw_data = b""
                # Read the exact number of bytes needed
                start_time = time.time()
                while len(raw_data) < EXPECTED_BYTES:
                    chunk = ser.read(EXPECTED_BYTES - len(raw_data))
                    if not chunk:
                        # Timeout
                        if time.time() - start_time > 5.0:
                            print("\nError: Timeout waiting for screen data.")
                            capturing = False
                            break
                        continue
                    raw_data += chunk
                    
                if capturing:
                    # Wait for the end marker line
                    end_line = ser.readline()
                    if b"---END_SCREENSHOT---" not in end_line:
                        # Read one more line in case of trailing noise
                        end_line = ser.readline()
                        
                    print(f" Done ({len(raw_data)} bytes received).")
                    
                    # Convert RGB565 to RGB888
                    image = Image.new("RGB", (WIDTH, HEIGHT))
                    pixels = []
                    
                    for i in range(0, len(raw_data), 2):
                        if i + 1 >= len(raw_data):
                            break
                        # Unpack 16-bit word (big-endian)
                        val = (raw_data[i] << 8) | raw_data[i+1]
                        
                        # Axiometa panel layout: BGR order in color565 mapping
                        # Bits 11-15: Blue, Bits 5-10: Green, Bits 0-4: Red.
                        # We extract them and scale to 0-255.
                        b_val = (val >> 11) & 0x1F
                        g_val = (val >> 5) & 0x3F
                        r_val = val & 0x1F
                        
                        r = int(r_val * 255 / 31)
                        g = int(g_val * 255 / 63)
                        b = int(b_val * 255 / 31)
                        
                        if args.swap_rgb:
                            # Swap Red and Blue if requested
                            pixels.append((b, g, r))
                        else:
                            pixels.append((r, g, b))
                            
                    image.putdata(pixels)
                    
                    # Save image
                    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                    filename = f"screenshot_{timestamp}.png"
                    filepath = os.path.join(output_dir, filename)
                    image.save(filepath)
                    print(f" Saved screenshot to: {filepath}\n")
                    
                capturing = False
                
    except KeyboardInterrupt:
        print("\nExiting screenshot utility.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
