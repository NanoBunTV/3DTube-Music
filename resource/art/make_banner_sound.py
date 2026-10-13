"""Generates resource/banner.wav : the short chime played when 3DTube Music is selected on the HOME menu.

A rising C major arpeggio (C5 E5 G5 C6) with a soft bell-like tone. Banner sounds must be under 3 seconds.
Usage : python resource/art/make_banner_sound.py   (no dependencies)
"""
import math
import os
import struct
import wave

RATE = 32000
LENGTH = 1.9  # seconds
NOTES = [(0.00, 523.25), (0.14, 659.25), (0.28, 783.99), (0.42, 1046.50)]  # (start, frequency)


def sample(t):
    value = 0.0
    for start, freq in NOTES:
        dt = t - start
        if dt < 0:
            continue
        attack = min(1.0, dt / 0.006)
        envelope = attack * math.exp(-dt * 3.2)
        # fundamental + a little octave and fifth for a bell-like timbre
        value += envelope * (math.sin(2 * math.pi * freq * dt) + 0.3 * math.sin(2 * math.pi * freq * 2 * dt) +
                             0.12 * math.sin(2 * math.pi * freq * 3 * dt))
    fade_out = min(1.0, (LENGTH - t) / 0.25)
    return value * 0.22 * fade_out


def main():
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "banner.wav")
    frames = bytearray()
    for i in range(int(RATE * LENGTH)):
        v = max(-1.0, min(1.0, sample(i / RATE)))
        frames += struct.pack("<h", int(v * 32767))
    with wave.open(out_path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(bytes(frames))
    print("wrote", os.path.normpath(out_path))


if __name__ == "__main__":
    main()
