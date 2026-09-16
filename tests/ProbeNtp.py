#!/usr/bin/env python3
"""Read NTP response metadata without adjusting either endpoint's clock."""

import argparse
import json
import math
import socket
import struct
import sys
import time


NTP_EPOCH_SECONDS = 2208988800
NTP_PACKET_SIZE = 48


def decode_response(packet, originate_timestamp, round_trip_ns):
    """Validate a reply and return reported precision and observable timing."""
    if len(packet) != NTP_PACKET_SIZE:
        raise ValueError(f"expected 48-byte reply, received {len(packet)} bytes")
    leap, version, mode = packet[0] >> 6, (packet[0] >> 3) & 7, packet[0] & 7
    if version != 4 or mode != 4:
        raise ValueError(f"expected NTP v4 server reply, got version {version}, mode {mode}")
    if packet[24:32] != originate_timestamp:
        raise ValueError("reply originate timestamp does not match this request")

    precision = struct.unpack_from('!b', packet, 3)[0]
    receive, transmit = struct.unpack_from('!QQ', packet, 32)
    # Signed modular subtraction also handles the 2036 NTP era rollover.
    delta = ((transmit - receive + (1 << 63)) % (1 << 64)) - (1 << 63)
    refid = packet[12:16]
    return {
        'version': version,
        'mode': mode,
        'leap': leap,
        'stratum': packet[1],
        'precision_exponent': precision,
        'precision_seconds': math.ldexp(1.0, precision),
        'reference_id_hex': refid.hex(),
        'reference_id_text': ''.join(chr(value) if 32 <= value <= 126 else '.' for value in refid),
        'root_dispersion_seconds': struct.unpack_from('!I', packet, 8)[0] / 65536.0,
        'round_trip_seconds': round_trip_ns / 1_000_000_000,
        'server_processing_seconds': delta / (1 << 32) if receive and transmit else None,
    }


def make_request():
    """Build a version 4 NTP request containing the current transmit timestamp."""
    request = bytearray(NTP_PACKET_SIZE)
    request[0] = (4 << 3) | 3  # Version 4, client mode.
    request[2] = 6
    seconds, nanoseconds = divmod(time.time_ns(), 1_000_000_000)
    struct.pack_into('!II', request, 40,
                     (seconds + NTP_EPOCH_SECONDS) & 0xFFFFFFFF,
                     (nanoseconds << 32) // 1_000_000_000)
    return request


def main():
    """Probe the requested NTP server at bounded intervals and print a JSON report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='NTP server hostname or IP address (UDP port 123)')
    parser.add_argument('--samples', type=int, default=5, help='number of requests (default: 5)')
    parser.add_argument('--timeout', type=float, default=2.0, help='reply timeout in seconds (default: 2)')
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('--samples must be at least 1')
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('--timeout must be a finite positive number')

    report = {
        'host': args.host,
        'port': 123,
        'samples_requested': args.samples,
        'samples_received': 0,
        'samples': [],
        'interpretation': 'Precision is reported by the server. RTT includes network and server delays. '
                          'These observations do not establish absolute clock accuracy.',
    }
    try:
        family, socktype, protocol, _, address = socket.getaddrinfo(
            args.host, 123, type=socket.SOCK_DGRAM)[0]
        with socket.socket(family, socktype, protocol) as connection:
            connection.settimeout(args.timeout)
            connection.connect(address)  # Restricts received datagrams to the selected endpoint.
            report['endpoint'] = list(connection.getpeername())
            last_start = None
            for index in range(args.samples):
                if last_start is not None:
                    time.sleep(max(0.0, 1.0 - (time.monotonic() - last_start)))
                request = make_request()
                last_start = time.monotonic()
                start_ns = time.monotonic_ns()
                try:
                    connection.send(request)
                    reply = connection.recv(65535)
                    elapsed_ns = time.monotonic_ns() - start_ns
                    sample = decode_response(reply, request[40:48], elapsed_ns)
                    report['samples_received'] += 1
                except (OSError, ValueError) as error:
                    sample = {'error': str(error)}
                sample['sample'] = index + 1
                report['samples'].append(sample)
    except OSError as error:
        report['error'] = str(error)

    print(json.dumps(report, indent=2))
    return 0 if report['samples_received'] == args.samples else 1


if __name__ == '__main__':
    sys.exit(main())
