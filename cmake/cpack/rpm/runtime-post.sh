#!/bin/sh
# Refresh dynamic linker cache after runtime package installation.
if [ -x /sbin/ldconfig ]; then
  /sbin/ldconfig >/dev/null 2>&1 || true
fi
if [ -x /usr/sbin/ldconfig ]; then
  /usr/sbin/ldconfig >/dev/null 2>&1 || true
fi
exit 0
