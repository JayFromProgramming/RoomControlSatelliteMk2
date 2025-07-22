import subprocess

def execute(cmd):
    popen = subprocess.Popen(cmd, stdout=subprocess.PIPE, universal_newlines=True, bufsize=650)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line
    popen.stdout.close()
    return_code = popen.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, cmd)

# Use addr2line to convert the inputed esp32 backtrace to the corresponding file and line numbers

def convert_backtrace_to_list(backtrace):
    segments = backtrace.split(' ')
    for segment in segments:
        if segment == '':
            continue
        if segment.startswith('0x'):
            yield segment.split(':')[0]

def run_addr2line(esp32_backtrace):
    addresses = list(convert_backtrace_to_list(esp32_backtrace))
    for line in execute(['wsl', 'addr2line', '-e', '.pio/build/nodemcu-32s2/firmware.elf', *addresses]):
        print(line, end='')

if __name__ == '__main__':
    backtrace = input("Enter the backtrace: ")
    run_addr2line(backtrace)


