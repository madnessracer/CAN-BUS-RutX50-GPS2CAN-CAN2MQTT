#!/usr/bin/env python3
# espota_router.py
# Saves a local espota.py copy to the router and runs it.

import os
import sys
import argparse
import subprocess
import hashlib

SCRIPT_PATH = "/tmp/mountd/disk1_part1/espota.py"
FIRMWARE_PATH = "/tmp/mountd/disk1_part1/firmwares/firmware.bin"

ESPOTA_PY_CONTENT = r'''#!/usr/bin/env python3
# Original espota.py by Ivan Grokhotkov / Espressif

from __future__ import print_function
import socket
import sys
import os
import optparse
import logging
import hashlib
import random

FLASH = 0
SPIFFS = 100
AUTH = 200
PROGRESS = False


def update_progress(progress):
    if (PROGRESS):
        barLength = 60
        status = ""
        if isinstance(progress, int):
            progress = float(progress)
        if not isinstance(progress, float):
            progress = 0
            status = "error: progress var must be float\r\n"
        if progress < 0:
            progress = 0
            status = "Halt...\r\n"
        if progress >= 1:
            progress = 1
            status = "Done...\r\n"
        block = int(round(barLength*progress))
        text = "\rUploading: [{0}] {1}% {2}".format("="*block + " "*(barLength-block), int(progress*100), status)
        sys.stderr.write(text)
        sys.stderr.flush()
    else:
        sys.stderr.write('.')
        sys.stderr.flush()


def serve(remoteAddr, localAddr, remotePort, localPort, password, filename, command = FLASH):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_address = (localAddr, localPort)
    logging.info('Starting on %s:%s', str(server_address[0]), str(server_address[1]))
    try:
        sock.bind(server_address)
        sock.listen(1)
    except:
        logging.error("Listen Failed")
        return 1

    content_size = os.path.getsize(filename)
    f = open(filename,'rb')
    file_md5 = hashlib.md5(f.read()).hexdigest()
    f.close()
    logging.info('Upload size: %d', content_size)
    message = '%d %d %d %s\n' % (command, localPort, content_size, file_md5)

    inv_trys = 0
    data = ''
    msg = 'Sending invitation to %s ' % (remoteAddr)
    sys.stderr.write(msg)
    sys.stderr.flush()
    while (inv_trys < 10):
        inv_trys += 1
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        remote_address = (remoteAddr, int(remotePort))
        try:
            sock2.sendto(message.encode(), remote_address)
        except:
            sys.stderr.write('failed\n')
            sys.stderr.flush()
            sock2.close()
            logging.error('Host %s Not Found', remoteAddr)
            return 1
        sock2.settimeout(10)
        try:
            data = sock2.recv(37).decode()
            break
        except:
            sys.stderr.write('.')
            sys.stderr.flush()
            sock2.close()
    sys.stderr.write('\n')
    sys.stderr.flush()
    if (inv_trys == 10):
        logging.error('No response from the ESP')
        return 1
    if (data != "OK"):
        if(data.startswith('AUTH')):
            tokens = data.split()
            if len(tokens) < 2:
                logging.error('AUTH response malformed: %s', data)
                sock2.close()
                return 1
            nonce = tokens[1]
            cnonce_text = '%s%u%s%s' % (filename, content_size, file_md5, remoteAddr)
            cnonce = hashlib.md5(cnonce_text.encode()).hexdigest()
            passmd5 = hashlib.md5(password.encode()).hexdigest()
            result_text = '%s:%s:%s' % (passmd5, nonce, cnonce)
            sys.stderr.write('Authenticating...')
            sys.stderr.flush()
            message = '%d %s %s\n' % (AUTH, cnonce, result_text)
            sock2.sendto(message.encode(), remote_address)
            sock2.settimeout(10)
            try:
                data = sock2.recv(32).decode()
            except:
                sys.stderr.write('FAIL\n')
                logging.error('No Answer to our Authentication')
                sock2.close()
                return 1
            if (data != "OK"):
                sys.stderr.write('FAIL\n')
                logging.error('%s', data)
                sock2.close()
                return 1
            sys.stderr.write('OK\n')
        else:
            logging.error('Bad Answer: %s', data)
            sock2.close()
            return 1
    sock2.close()

    logging.info('Waiting for device...')
    try:
        sock.settimeout(10)
        connection, client_address = sock.accept()
        sock.settimeout(None)
        connection.settimeout(None)
    except:
        logging.error('No response from device')
        sock.close()
        return 1
    try:
        f = open(filename, "rb")
        if (PROGRESS):
            update_progress(0)
        else:
            sys.stderr.write('Uploading')
            sys.stderr.flush()
        offset = 0
        while True:
            chunk = f.read(1024)
            if not chunk: break
            offset += len(chunk)
            update_progress(offset/float(content_size))
            connection.settimeout(10)
            try:
                connection.sendall(chunk)
                res = connection.recv(10)
                lastResponseContainedOK = 'OK' in res.decode()
            except:
                sys.stderr.write('\n')
                logging.error('Error Uploading')
                connection.close()
                f.close()
                sock.close()
                return 1

        if lastResponseContainedOK:
            logging.info('Success')
            connection.close()
            f.close()
            sock.close()
            return 0

        sys.stderr.write('\n')
        logging.info('Waiting for result...')
        try:
            count = 0
            while True:
                count += 1
                connection.settimeout(60)
                data = connection.recv(32).decode()
                logging.info('Result: %s' ,data)

                if "OK" in data:
                    logging.info('Success')
                    connection.close()
                    f.close()
                    sock.close()
                    return 0
                if count == 5:
                    logging.error('Error response from device')
                    connection.close()
                    f.close()
                    sock.close()
                    return 1
        except Exception:
            logging.error('No Result!')
            connection.close()
            f.close()
            sock.close()
            return 1
    finally:
        connection.close()
        f.close()
    sock.close()
    return 1


def parser(unparsed_args):
    parser = optparse.OptionParser(
        usage = "%prog [options]",
        description = "Transmit image over the air to the esp32 module with OTA support."
    )

    group = optparse.OptionGroup(parser, "Destination")
    group.add_option("-i", "--ip",
        dest = "esp_ip",
        action = "store",
        help = "ESP32 IP Address.",
        default = False
    )
    group.add_option("-I", "--host_ip",
        dest = "host_ip",
        action = "store",
        help = "Host IP Address.",
        default = "0.0.0.0"
    )
    group.add_option("-p", "--port",
        dest = "esp_port",
        type = "int",
        help = "ESP32 ota Port. Default 3232",
        default = 3232
    )
    group.add_option("-P", "--host_port",
        dest = "host_port",
        type = "int",
        help = "Host server ota Port. Default random 10000-60000",
        default = random.randint(10000,60000)
    )
    parser.add_option_group(group)

    group = optparse.OptionGroup(parser, "Authentication")
    group.add_option("-a", "--auth",
        dest = "auth",
        help = "Set authentication password.",
        action = "store",
        default = ""
    )
    parser.add_option_group(group)

    group = optparse.OptionGroup(parser, "Image")
    group.add_option("-f", "--file",
        dest = "image",
        help = "Image file.",
        metavar="FILE",
        default = None
    )
    group.add_option("-s", "--spiffs",
        dest = "spiffs",
        action = "store_true",
        help = "Use this option to transmit a SPIFFS image and do not flash the module.",
        default = False
    )
    parser.add_option_group(group)

    group = optparse.OptionGroup(parser, "Output")
    group.add_option("-d", "--debug",
        dest = "debug",
        help = "Show debug output. And override loglevel with debug.",
        action = "store_true",
        default = False
    )
    group.add_option("-r", "--progress",
        dest = "progress",
        help = "Show progress output. Does not work for ArduinoIDE",
        action = "store_true",
        default = False
    )
    group.add_option("-t", "--timeout",
        dest = "timeout",
        type = "int",
        help = "Timeout to wait for the ESP32 to accept invitation",
        default = 10
    )
    parser.add_option_group(group)

    (options, args) = parser.parse_args(unparsed_args)
    return options


def main(args):
    options = parser(args)
    loglevel = logging.WARNING
    if (options.debug):
        loglevel = logging.DEBUG

    logging.basicConfig(level = loglevel, format = '%(asctime)-8s [%(levelname)s]: %(message)s', datefmt = '%H:%M:%S')
    logging.debug("Options: %s", str(options))

    global PROGRESS, TIMEOUT
    PROGRESS = options.progress
    TIMEOUT = options.timeout

    if (not options.esp_ip or not options.image):
        logging.critical("Not enough arguments.")
        return 1

    command = FLASH
    if (options.spiffs):
        command = SPIFFS

    return serve(options.esp_ip, options.host_ip, options.esp_port, options.host_port, options.auth, options.image, command)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
'''


def remote_file_hash(router_host, remote_path):
    cmd = [
        'ssh', router_host,
        'python3', '-c',
        "import hashlib; print(hashlib.sha256(open(r'{0}','rb').read()).hexdigest())".format(remote_path)
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out, err = proc.communicate()
    if proc.returncode != 0:
        return None
    return out.strip()


def write_espota(router_host):
    remote_path = SCRIPT_PATH
    local_hash = hashlib.sha256(ESPOTA_PY_CONTENT.encode('utf-8')).hexdigest()

    existing_hash = remote_file_hash(router_host, remote_path)
    if existing_hash == local_hash:
        cmd = ['ssh', router_host, 'chmod +x {}'.format(remote_path)]
        subprocess.run(cmd)
        return True

    cmd = [
        'ssh', router_host,
        'cat > {} && chmod +x {}'.format(remote_path, remote_path)
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, text=True)
    proc.communicate(ESPOTA_PY_CONTENT)
    return proc.returncode == 0


def transfer_firmware(router_host, source_file):
    cmd = [
        'ssh', router_host,
        'cat > {} && echo ok'.format(FIRMWARE_PATH)
    ]
    with open(source_file, 'rb') as f:
        firmware_data = f.read()
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate(firmware_data)
    if proc.returncode != 0 or out.strip() != b'ok':
        sys.stderr.write(err.decode('utf-8', errors='replace') if isinstance(err, bytes) else str(err))
        return False
    return True


def run_remote_espota(router_host, target_ip, local_ip, timeout, source_file):
    if not transfer_firmware(router_host, source_file):
        print('Failed to transfer firmware to router')
        return False

    remote_cmd = (
        'python3 {script} -d -i {target} -I {local} -t {timeout} -f {file}'
        .format(script=SCRIPT_PATH, target=target_ip, local=local_ip, timeout=timeout, file=FIRMWARE_PATH)
    )
    proc = subprocess.Popen(['ssh', router_host, remote_cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out, err = proc.communicate()
    sys.stdout.write(out)
    sys.stderr.write(err)
    return proc.returncode == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--router', default='root@172.24.115.140', help='Router user@host')
    parser.add_argument('--target', required=True, help='Target ESP IP address')
    parser.add_argument('--local-ip', default='172.24.115.140', help='Router local IP address for OTA host')
    parser.add_argument('--timeout', default='120', help='OTA invitation timeout in seconds')
    parser.add_argument('--file', required=True, help='Firmware file to upload')
    args = parser.parse_args()

    if not write_espota(args.router):
        print('Failed to write espota.py to router')
        return 1

    print('espota.py written to router, starting remote OTA...')
    if not run_remote_espota(args.router, args.target, args.local_ip, args.timeout, args.file):
        print('Remote OTA failed')
        return 1

    print('Remote OTA succeeded')
    return 0

if __name__ == '__main__':
    sys.exit(main())
