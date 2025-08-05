import os
import requests


class FirmwareMover:
    endpoint = 'http://localhost/satellite_firmware_upload'

    def __init__(self):
        self.build_version = ''
        self.build_branch = 'UNKNOWN'

    def get_build_info(self):
        with open('src/build_info.h') as f:
            lines = f.readlines()  # Read the first line to get the build number
            if len(lines) != 0:
                self.build_version = lines[4].split()[2] if len(lines) > 0 else 'UNKNOWN'
                self.build_version = self.build_version.strip('"')
            self.build_branch = lines[9].split()[2] if len(lines) > 9 else 'UNKNOWN'
            self.build_branch = self.build_branch.strip('"')  # Remove quotes if present

    def get_firmware_file(self):
        path = os.path.join(os.getcwd(), '.pio', 'build', 'nodemcu-32s2', 'firmware.bin')
        if os.path.isfile(path):
            return path
        else:
            raise FileNotFoundError(f"Firmware file not found at {path}")

    def upload_firmware(self):
        print(f"Uploading firmware to {self.endpoint}")
        self.get_build_info()
        firmware_file = self.get_firmware_file()
        request = f"{self.endpoint}?version={self.build_version}&branch={self.build_branch}"
        with open(firmware_file, 'rb') as f:
            response = requests.post(request, files={'file': f})
            if response.status_code == 200:
                print(f"Firmware uploaded successfully: {response.text}")
            else:
                print(f"Failed to upload firmware: {response.status_code} - {response.text}")


mover = FirmwareMover()


def upload_firmware(*args, **kwargs):
    mover.upload_firmware()
