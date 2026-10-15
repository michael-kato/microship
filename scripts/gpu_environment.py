"""Reject hardware tracing after the recorded AMD driver lockup."""

import os
import sys


def check_gpu_environment():
    flags = sorted(name for name, value in os.environ.items()
                   if value and name.startswith(('MESA_VK_TRACE', 'RADV_THREAD_TRACE')))
    if flags:
        raise RuntimeError('Hardware tracing is disabled after the GPU lockup. '
                           'Unset these variables before running Floria: ' + ', '.join(flags))


if __name__ == '__main__':
    try:
        check_gpu_environment()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
