"""Read optional host timeouts for GPU tools. There is no fixed GPU work budget."""

import os
import math


def gpu_process_timeout_seconds():
    """Return a configurable timeout for a process that stops responding."""
    seconds = float(os.environ.get('NANOVORE_GPU_TIMEOUT_SECONDS', '120'))
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError('NANOVORE_GPU_TIMEOUT_SECONDS must be finite and positive')
    return seconds


def check_gpu_environment():
    gpu_process_timeout_seconds()


if __name__ == '__main__':
    check_gpu_environment()
