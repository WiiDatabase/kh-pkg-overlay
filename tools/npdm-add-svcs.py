#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "click",
# ]
# ///
import struct
from pathlib import Path

import click

EXLAUNCH_SVCS = range(0x80)


def svc_descriptors(caps: bytes) -> dict[int, int]:
    out = {}
    for (d,) in struct.iter_unpack("<I", caps):
        if d & 0x1F == 0x0F:
            out[d >> 29] = out.get(d >> 29, 0) | (d >> 5) & 0xFFFFFF
    return out


def add_svcs(caps: bytes, svcs: range) -> bytes:
    have = svc_descriptors(caps)
    want: dict[int, int] = {}
    for s in svcs:
        want[s // 24] = want.get(s // 24, 0) | 1 << (s % 24)
    kept = []
    for (d,) in struct.iter_unpack("<I", caps):
        if d & 0x1F == 0x0F and d >> 29 in want:
            continue
        kept.append(d)
    for idx in sorted(set(have) | set(want)):
        if idx in want:
            mask = have.get(idx, 0) | want[idx]
            kept.append(idx << 29 | mask << 5 | 0x0F)
    return struct.pack(f"<{len(kept)}I", *kept)


def align(n: int, a: int) -> int:
    return n + (-n % a)


@click.command()
@click.argument("src", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.argument("dst", type=click.Path(dir_okay=False, path_type=Path))
def main(src: Path, dst: Path) -> None:
    """Copy the game's main.npdm SRC to DST with all SVCs 0x00-0x7F enabled in ACI0 and ACID, as exlaunch needs."""
    d = src.read_bytes()
    if d[:4] != b"META":
        raise click.ClickException("not an NPDM")
    aci_off, aci_sz, acid_off, acid_sz = struct.unpack_from("<IIII", d, 0x70)
    if not acid_off < aci_off:
        raise click.ClickException("expected ACID before ACI0")
    acid = bytearray(d[acid_off : acid_off + acid_sz])
    aci = bytearray(d[aci_off : aci_off + aci_sz])

    akc_off, akc_sz = struct.unpack_from("<II", acid, 0x230)
    kc_off, kc_sz = struct.unpack_from("<II", aci, 0x30)
    if akc_off + akc_sz != len(acid) or kc_off + kc_sz != len(aci):
        raise click.ClickException("kernel capabilities are not the last block; layout not supported")

    new_akc = add_svcs(bytes(acid[akc_off:]), EXLAUNCH_SVCS)
    new_kc = add_svcs(bytes(aci[kc_off:]), EXLAUNCH_SVCS)
    acid = acid[:akc_off] + new_akc
    aci = aci[:kc_off] + new_kc
    struct.pack_into("<I", acid, 0x234, len(new_akc))
    struct.pack_into("<I", acid, 0x204, len(acid) - 0x100)
    struct.pack_into("<I", aci, 0x34, len(new_kc))

    head = bytearray(d[:acid_off])
    new_aci_off = align(acid_off + len(acid), 0x10)
    struct.pack_into("<IIII", head, 0x70, new_aci_off, len(aci), acid_off, len(acid))
    out = head + acid + bytes(new_aci_off - acid_off - len(acid)) + aci
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bytes(out))
    click.echo(f"{dst}: {len(d)} -> {len(out)} bytes, SVCs 0x00-0x7F enabled")


if __name__ == "__main__":
    main()
