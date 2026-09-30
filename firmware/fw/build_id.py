# PlatformIO pre-script: BUILD_ID env var -> -DBUILD_ID="..." (default "dev").
import os, re
Import("env")  # noqa: F821

bid = os.environ.get("BUILD_ID", "").strip() or "dev"
if not re.fullmatch(r"[A-Za-z0-9._:+-]{1,48}", bid):
    raise SystemExit("BUILD_ID must match [A-Za-z0-9._:+-]{1,48}, got %r" % bid)
env.Append(CPPDEFINES=[("BUILD_ID", env.StringifyMacro(bid))])  # noqa: F821
print("usbPD BUILD_ID=%s" % bid)
