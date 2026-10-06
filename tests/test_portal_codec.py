"""Compile the actual portal codec for the host and exercise its input boundaries.

Windows: python tests/test_portal_codec.py --zig <zig.exe>
Linux/macOS: python tests/test_portal_codec.py (uses system clang)
"""
import argparse
import ctypes
import os
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def compile_codec(clang, zig=None):
    output = ROOT / "build" / "portal_tests"
    output.mkdir(parents=True, exist_ok=True)
    if zig:
        library = output / ("portal_codec.dll" if sys.platform == "win32" else "portal_codec.so")
        exports = output / "exports.def"
        exports.write_text("EXPORTS\nportal_credentials_valid\nportal_dns_reply\nportal_address_matches_ipv4\n", encoding="utf-8")
        command = [zig, "cc", "-shared", "-O1", "-Wall", "-Wextra", "-Werror",
                   str(ROOT / "main" / "portal_codec.c"), "-o", str(library)]
        if sys.platform == "win32":
            command.append(str(exports))
        else:
            command.append("-fPIC")
        env = os.environ.copy()
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", str(output / "zig_cache"))
        env.setdefault("ZIG_LOCAL_CACHE_DIR", str(output / "zig_local"))
        subprocess.run(command, check=True, env=env)
    elif sys.platform == "win32":
        # No Windows SDK is needed: this parser uses only these three libc calls.
        (output / "string.h").write_text(
            "#include <stddef.h>\n"
            "size_t strlen(const char *);\n"
            "size_t strspn(const char *, const char *);\n"
            "void *memcpy(void *, const void *, size_t);\n", encoding="utf-8"
        )
        (output / "runtime.c").write_text(
            "#include <stddef.h>\n"
            "size_t strlen(const char *s) { size_t n=0; while(s[n]) ++n; return n; }\n"
            "size_t strspn(const char *s, const char *a) { size_t n=0; "
            "while(s[n]) { const char *p=a; while(*p && *p!=s[n]) ++p; "
            "if(!*p) break; ++n; } return n; }\n"
            "void *memcpy(void *d, const void *s, size_t n) { unsigned char *p=d; "
            "const unsigned char *q=s; for(size_t i=0;i<n;++i) p[i]=q[i]; return d; }\n",
            encoding="utf-8"
        )
        objects = []
        for source in [ROOT / "main" / "portal_codec.c", output / "runtime.c"]:
            obj = output / (source.stem + ".obj")
            subprocess.run([clang, "--target=x86_64-pc-windows-msvc", "-c", "-O1",
                            "-ffreestanding", "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(output), str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        library = output / "portal_codec.dll"
        linker = Path(clang).resolve().with_name("lld.exe")
        subprocess.run([str(linker), "-flavor", "link", "/dll", "/noentry", "/nodefaultlib",
                        "/export:portal_credentials_valid", "/export:portal_dns_reply", "/export:portal_address_matches_ipv4",
                        "/out:" + str(library), *objects], check=True)
    else:
        library = output / "portal_codec.so"
        subprocess.run([clang, "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "main" / "portal_codec.c"), "-o", str(library)], check=True)
    return ctypes.CDLL(str(library))


def run_tests(lib):
    address_matches = lib.portal_address_matches_ipv4
    address_matches.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    address_matches.restype = ctypes.c_bool
    ap = bytes([192, 168, 4, 1])
    mapped_prefix = b"\0" * 10 + b"\xff\xff"
    assert address_matches(ap, 4, ap)
    assert address_matches(mapped_prefix + ap, 16, ap) # current IDF dual-stack socket
    assert not address_matches(bytes([192, 168, 1, 1]), 4, ap)
    assert not address_matches(mapped_prefix + bytes([192, 168, 1, 1]), 16, ap)
    assert not address_matches(b"\0" * 12 + ap, 16, ap) # IPv4-compatible != mapped
    assert not address_matches(b"\xfe\x80" + b"\0" * 10 + ap, 16, ap) # native IPv6
    assert not address_matches(mapped_prefix + ap, 15, ap) # truncated sockaddr
    assert not address_matches(None, 4, ap)
    assert not address_matches(ap, 4, None)
    for byte in range(12):
        mutated = bytearray(mapped_prefix + ap)
        mutated[byte] ^= 1
        assert not address_matches(bytes(mutated), 16, ap), byte
    valid = lib.portal_credentials_valid
    valid.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    valid.restype = ctypes.c_bool
    cases = [
        (b"home", b"", True), (b"", b"12345678", False),
        (b"a" * 32, b"12345678", True), (b"a" * 33, b"12345678", False),
        ("家庭网络".encode(), b"correct password", True),
        ("家".encode() * 11, b"12345678", False),
        (b"home", b"short", False), (b"home", b" " * 8, True),
        (b"home", b"p" * 63, True), (b"home", b"p" * 64, False),
        (b"home", b"A9" * 32, True), (b"home", b"A" * 65, False),
        (b"home", b"hello\nworld", False), (b"home", b"hello\xffworld", False),
        (None, b"", False), (b"home", None, False),
    ]
    for ssid, password, expected in cases:
        assert valid(ssid, password) == expected, (ssid, expected)

    reply = lib.portal_dns_reply
    reply.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.c_size_t]
    reply.restype = ctypes.c_size_t

    def check(data, capacity=512):
        buffer = (ctypes.c_uint8 * 1024)()
        ctypes.memset(buffer, 0xA5, len(buffer))
        buffer[:len(data)] = data
        original = bytes(buffer)
        result = reply(buffer, len(data), capacity)
        assert result <= capacity
        assert bytes(buffer)[capacity:] == original[capacity:], "write beyond capacity"
        if result == 0:
            assert bytes(buffer) == original, "rejected packet was modified"
        return bytes(buffer[:result])

    header = struct.pack("!6H", 0x1234, 0x0100, 1, 0, 0, 0)
    name = b"\x07example\x03com\0"
    packet = header + name + struct.pack("!2H", 1, 1)
    response = check(packet)
    assert response[:2] == b"\x12\x34"
    assert struct.unpack("!6H", response[:12]) == (0x1234, 0x8100, 1, 1, 0, 0)
    assert response[12:len(packet)] == packet[12:]
    assert response[-4:] == bytes([192, 168, 4, 1])
    assert len(response) == len(packet) + 16
    assert len(check(header + name + struct.pack("!2H", 28, 1))) == len(packet) # AAAA: no A answer
    assert check(response) == b"" # not a query
    assert check(header[:5] + b"\x02" + header[6:] + packet[12:]) == b"" # two questions
    assert check(header + b"\xc0\x0c" + b"\0\1\0\1") == b"" # compression pointer/loop
    assert check(header + b"\x40" + b"a" * 64 + b"\0\0\1\0\1") == b"" # invalid label
    assert check(packet, len(packet) + 15) == b"" # insufficient answer capacity
    for size in range(len(packet)):
        assert check(packet[:size]) == b"", size
    edns = bytearray(packet)
    edns[11] = 1
    edns.extend(b"\0\0\x29\x10\0\0\0\0\0\0\0")
    assert len(check(bytes(edns))) == len(packet) + 16

    rng = random.Random(20261005)
    for _ in range(20000):
        data = bytearray(rng.randbytes(rng.randrange(513)))
        if rng.random() < .5:
            data = bytearray(packet)
            for _ in range(rng.randrange(1, 8)):
                data[rng.randrange(len(data))] = rng.randrange(256)
        check(bytes(data), rng.randrange(len(data), 513))
    print(f"PASS: IPv4/IPv4-mapped IPv6 interface checks, {len(cases)} credential cases, "
          "DNS protocol/truncation cases, 20,000 bounded fuzz cases")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default=shutil.which("clang"))
    parser.add_argument("--zig", default=None)
    args = parser.parse_args()
    if not args.clang and not args.zig:
        parser.error("Provide --clang or --zig with the path to a host compiler")
    run_tests(compile_codec(args.clang, args.zig))
