"""
uPhonor Python Bindings

Real-time audio looping system with Python CFFI bindings.
"""

from ._version import __version__
from .uphonor_python import *

__author__ = "Quaternion Media"
__email__ = "holophonor@quaternion.media"

__all__ = [
    'UPhonor',
    'MemoryLoop',
    'HoloState',
    'LoopState',
    'PlaybackMode',
    'ConfigResult',
    'uphonor_session',
    'create_uphonor',
    '__version__',
]
