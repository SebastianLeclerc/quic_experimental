import struct
import codecs
import os

# Reads from file created by: mosquitto_sub -h 127.0.0.1 -t "__edge/#" -v >> edge_timestamps.log
# Requires EMQX Rule that republishes egress traffic

OUTPUT_FILE = "egress.log"
HEADER = "topic,egresstime,seq\n"

# Create file and write header if it does not exist yet
if not os.path.exists(OUTPUT_FILE):
    with open(OUTPUT_FILE, "w") as out:
        out.write(HEADER)

with open("edge_timestamps.log", "r", encoding="utf-8", errors="ignore") as f, \
     open(OUTPUT_FILE, "a") as out:

    for line in f:
        if "__edge/egress" not in line:
            continue

        # -------- topic --------
        try:
            topic = line.split('"topic":"', 1)[1].split('"', 1)[0]
        except IndexError:
            continue

        # -------- egress timestamp --------
        try:
            egress_time = (
                line.split('"t_edge_egress":', 1)[1]
                .split(",", 1)[0]
                .strip()
            )
        except IndexError:
            continue

        # -------- payload (robust extraction) --------
        payload_key = '"payload":"'
        start = line.find(payload_key)
        if start == -1:
            continue
        start += len(payload_key)

        end = line.find('","clientid"', start)
        if end == -1:
            end = line.rfind('"')

        payload_part = line[start:end]

        # Decode JSON unicode escapes → characters
        try:
            payload_str = codecs.decode(payload_part, "unicode_escape")
        except Exception:
            continue

        payload = payload_str.encode("latin1")

        if len(payload) < 16:
            continue

        # -------- binary decode (BIG-endian; matches your pub.c) --------
        seq, _send_ts = struct.unpack_from(">QQ", payload, 0)

        # -------- write CSV row --------
        out.write(f"{topic},{egress_time},{seq}\n")
