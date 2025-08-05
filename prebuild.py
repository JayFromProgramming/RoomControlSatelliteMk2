# Written by: Jay Sweeney (ajsweene@mtu.edu)

Import("env")

import postbuild
from datetime import datetime
from subprocess import CalledProcessError

import configparser

env.AddPostAction("buildprog", postbuild.upload_firmware)

# Add build info to build_info.h so it can be included in the firmware

# Build info is comprised of:
# - Build date
# - Build time
# - Build number (incremented each time this script is run)
# - Build type (debug or release)
# - Build git hash
# - Build git branch
# - Compiler version
# - Machine name

def get_git_hash():
    import subprocess
    return subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode('ascii').strip()


def get_git_branch():
    import subprocess
    return subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).decode('ascii').strip()


def get_machine_name():
    import socket
    return socket.gethostname()


def get_build_type():
    import os
    # Check if platformio.ini exists and check if there is a -g build flag
    if os.path.isfile('platformio.ini'):
        # Parse the ini
        config = configparser.ConfigParser()
        config.read('platformio.ini')
        # Check if the build flags section exists
        section = config.sections()[0]
        # Check for a 'build_flags' config in that section
        if 'build_flags' in config[section]:
            # Check if the build flags contain '-g'
            if '-g' in config[section]['build_flags']:
                return 'DEBUG'
        return 'RELEASE'


# if build_info.h doesn't exist, create it with no content
try:
    open('src/build_info.h')
except IOError:
    open('src/build_info.h', 'w').close()

with open('src/build_info.h') as f:
    lines = f.readlines()  # Read the first line to get the build number
    if len(lines) == 0:
        build_number_minor = 1
        build_number_major = 0
        build_number_breaking = 0
    else:
        build_number_minor = int(lines[1].split()[2]) + 1  # Increment the build number
        build_number_major = int(lines[2].split()[2])
        build_number_breaking = int(lines[3].split()[2])
    build_date = datetime.now().strftime("%Y-%m-%d")
    build_time = datetime.now().strftime("%H:%M:%S")
    try:
        build_type = get_build_type()
        build_git_hash = get_git_hash()
        build_git_branch = get_git_branch()
        build_machine_name = get_machine_name()
    except CalledProcessError:
        build_type = 'UNKNOWN'
        build_git_hash = 'UNKNOWN'
        build_git_branch = 'UNKNOWN'
        build_machine_name = 'UNKNOWN'

# Write the build info to build_info.h
with open('src/build_info.h', 'w') as f:
    f.write("#pragma once\n")
    f.write(f"#define BUILD_NUMBER_MINOR {build_number_minor}\n")
    f.write(f"#define BUILD_NUMBER_MAJOR {build_number_major}\n")
    f.write(f"#define BUILD_NUMBER_BREAKING {build_number_breaking}\n")
    f.write(f"#define BUILD_VERSION \"v{build_number_breaking}.{build_number_major:}.{build_number_minor:03}\"\n")
    f.write("#define BUILD_DATE \"" + build_date + "\"\n")
    f.write("#define BUILD_TIME \"" + build_time + "\"\n")
    f.write("#define BUILD_TYPE \"" + build_type + "\"\n")
    if build_type == 'DEBUG':
        f.write("#define BUILD_DEBUG 1\n")
    f.write("#define BUILD_GIT_HASH \"" + build_git_hash + "\"\n")
    f.write("#define BUILD_GIT_BRANCH \"" + build_git_branch + "\"\n")
    f.write("#define BUILD_MACHINE_NAME \"" + build_machine_name + "\"\n")

print(f"Build number: {build_number_major}.{build_number_minor}")
print("Build date: " + build_date)
print("Build time: " + build_time)
print("Build type: " + build_type)
print("Build git hash: " + build_git_hash)
print("Build git branch: " + build_git_branch)
print("Build machine name: " + build_machine_name)
