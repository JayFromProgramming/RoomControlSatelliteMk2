import os
from datetime import datetime
from paramiko import SSHClient, AutoAddPolicy


class FirmwareMover:

    def __init__(self):
        self.build_number_minor = 1
        self.build_number_major = 0
        self.build_number_breaking = 0
        self.build_branch = 'UNKNOWN'

    def get_build_info(self):
        with open('src/build_info.h') as f:
            lines = f.readlines()  # Read the first line to get the build number
            if len(lines) == 0:
                self.build_number_minor = 1
                self.build_number_major = 0
                self.build_number_breaking = 0
            else:
                self.build_number_minor = int(lines[1].split()[2]) + 1  # Increment the build number
                self.build_number_major = int(lines[2].split()[2])
                self.build_number_breaking = int(lines[3].split()[2])
            self.build_branch = lines[10].split()[2] if len(lines) > 10 else 'UNKNOWN'

    def get_firmware_file(self):
        path = os.path.join('.pio', 'build', 'firmware.bin')
        if os.path.isfile(path):
            return path
        else:
            raise FileNotFoundError(f"Firmware file not found at {path}")
