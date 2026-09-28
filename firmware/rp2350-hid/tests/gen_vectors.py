#!/usr/bin/env python3
"""Turn docs/rp2350-protocol-vectors.json into a C header for test_vectors.c."""
import json
import sys


def hexbytes(h):
    b = bytes.fromhex(h)
    return "{" + ", ".join("0x%02x" % x for x in b) + "}" if b else "{0}", len(b)


def main(src, dst):
    d = json.load(open(src, encoding="utf-8"))
    out = ["// Generated from docs/rp2350-protocol-vectors.json by gen_vectors.py. Do not edit.", ""]
    out.append("#define VEC_CRC_CHECK 0x%04x" % int(d["crc"]["check"], 16))
    out.append("#define VEC_CRC_CHECK_ASCII %s" % json.dumps(d["crc"]["check_ascii"]))
    out.append("typedef struct { const char *name; int to_board; uint8_t type, seq; uint8_t payload[40]; size_t plen;"
               " uint8_t frame[48]; size_t flen; } vec_frame_t;")
    out.append("static const vec_frame_t VEC_FRAMES[] = {")
    for f in d["frames"]:
        p, pl = hexbytes(f["payload_hex"])
        fr, fl = hexbytes(f["frame_hex"])
        out.append('  {"%s", %d, %d, %d, %s, %d, %s, %d},' % (
            f["name"], 1 if f["direction"] == "host_to_board" else 0, f["type"], f["seq"], p, pl, fr, fl))
    out.append("};")
    out.append("typedef struct { uint8_t type, seq; uint8_t payload[40]; size_t plen; } vec_expect_t;")
    out.append("typedef struct { const char *name; uint8_t stream[128]; size_t slen; int rx_errors;"
               " size_t nexpect; vec_expect_t expect[8]; } vec_stream_t;")
    out.append("static const vec_stream_t VEC_STREAMS[] = {")
    for s in d["decode_streams"]:
        st, sl = hexbytes(s["stream_hex"])
        ex = []
        for e in s["expect"]:
            p, pl = hexbytes(e["payload_hex"])
            ex.append("{%d, %d, %s, %d}" % (e["type"], e["seq"], p, pl))
        out.append('  {"%s", %s, %d, %d, %d, {%s}},' % (s["name"], st, sl, s["rx_errors"], len(ex), ", ".join(ex)))
    out.append("};")
    open(dst, "w", encoding="utf-8").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
