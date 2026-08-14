import struct

def make_fat12():
    # 16 sectors, 512 bytes each (8KB)
    disk = bytearray(16 * 512)
    # Boot sector (Sector 0) matching TinyUSB exactly
    boot = bytes([
      0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30, 0x00, 0x02, 0x01, 0x01, 0x00,
      0x01, 0x10, 0x00, 0x10, 0x00, 0xF8, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0x34, 0x12, 0x00, 0x00, ord('T'), ord('i'), ord('n'), ord('y'), ord('U'),
      ord('S'), ord('B'), ord(' '), ord('M'), ord('S'), ord('C'), 0x46, 0x41, 0x54, 0x31, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00
    ])
    boot += b'\x00' * (510 - len(boot)) + b'\x55\xaa'
    disk[0:512] = boot
    
    # FAT (Sector 1)
    fat = bytearray(512)
    fat[0:3] = b'\xf8\xff\xff' # Media type and EOF for cluster 0 and 1
    # We will use cluster 2 for our file.
    # In FAT12, cluster 2 = 0xFFF takes bytes 3 and 4
    fat[3] = 0xFF
    fat[4] = 0x0F
    disk[512:1024] = fat
    
    # Root Dir (Sector 2)
    root = bytearray(512)
    
    # Volume Label
    root[0:11] = b'CONFIG     '
    root[11] = 0x08 # Volume label attribute
    root[22:26] = b'\x4F\x6D\x65\x43' # Dummy time/date
    
    # File entry
    root[32:43] = b'CONFIG  TXT'
    root[43] = 0x20 # Archive attribute
    root[32+14:32+18] = b'\x4F\x6D\x65\x43' # Dummy creation time/date
    root[32+22:32+26] = b'\x4F\x6D\x65\x43' # Dummy modification time/date
    root[58] = 2 # First cluster = 2
    root[59] = 0
    
    data = b'pan_center=1500\r\ntilt_center=1500\r\npan_range=1000\r\ntilt_range=1000\r\nswap_pan_tilt=0\r\ndmx_start_address=1\r\ntest_mode=0\r\n'
    # Update file size in root dir
    root[60:64] = struct.pack('<I', len(data))
    disk[1024:1536] = root
    
    # File data at sector 3
    disk[1536:1536+len(data)] = data
    
    with open('disk_image.h', 'w') as f:
        f.write('#ifndef DISK_IMAGE_H\n')
        f.write('#define DISK_IMAGE_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write('const uint8_t default_disk_image[] = {\n')
        for i in range(0, len(disk), 16):
            chunk = disk[i:i+16]
            f.write('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
        f.write('};\n\n')
        f.write('#endif\n')

if __name__ == '__main__':
    make_fat12()
