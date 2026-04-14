Import("env")
from serial.tools import list_ports

# ---------- helpers ----------
def pio_opt(name, default=None):
    # Lee una opción del env actual. Si no existe, devuelve default.
    try:
        return env.GetProjectOption(name)
    except Exception:
        try:
            cfg = env.GetProjectConfig()
            section = "env:" + env["PIOENV"]
            return cfg.get(section, name, default)
        except Exception:
            return default

KNOWN_USB = {
    (0x10C4, 0xEA60),  # CP210x
    (0x1A86, 0x7523),  # CH340
    (0x0403, 0x6001),  # FTDI
    (0x303A, 0x1001),  # USB-JTAG/Serial de ESP32-Sx (si aplica)
}

def find_serial_port():
    preferred, fallback = None, None
    for p in list_ports.comports():
        if p.vid and p.pid and (p.vid, p.pid) in KNOWN_USB:
            preferred = p.device
            break
        desc = (p.description or "") + " " + (p.manufacturer or "")
        if fallback is None and any(x in desc for x in ("USB", "Silicon Labs", "FTDI", "wch.cn", "CP210", "CH340")):
            fallback = p.device
    return preferred or fallback

port = find_serial_port()
if port:
    print(f"[auto_uploader] Puerto serie detectado: {port} -> esptool (serial)")
    env.Replace(UPLOAD_PROTOCOL="esptool", UPLOAD_PORT=port)
    # env.Replace(UPLOAD_SPEED=921600)  # opcional
else:
    host = pio_opt("custom_ota_host", "ph-remote.local")
    auth = pio_opt("custom_ota_auth", None)
    print(f"[auto_uploader] Sin puerto serie -> OTA (espota) a {host}")
    env.Replace(UPLOAD_PROTOCOL="espota", UPLOAD_PORT=host)
    if auth:
        env.Append(UPLOAD_FLAGS=["--auth", auth])
    # env.Append(UPLOAD_FLAGS=["--port", "3232"])  # si necesitas cambiar el puerto OTA
