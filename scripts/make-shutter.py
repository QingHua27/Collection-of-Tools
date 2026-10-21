"""Generate a short mechanical camera shutter cue, 44.1 kHz mono PCM."""
from pathlib import Path
import math, random, struct, wave
rate=44100
rng=random.Random(714)
samples=[]
last=0.0
for i in range(int(rate*0.22)):
    t=i/rate
    noise=rng.uniform(-1,1)
    high=noise-last*0.78
    last=noise
    envelope=0.0
    for start,amp,decay in [(0.004,0.68,0.012),(0.043,0.95,0.019),(0.100,0.42,0.016)]:
        dt=t-start
        if dt>=0: envelope+=amp*math.exp(-dt/decay)*min(1,dt/0.0008)
    metal=math.sin(2*math.pi*1850*t)*math.exp(-t/0.038)*0.12
    value=(high*0.52+metal)*envelope
    samples.append(struct.pack('<h',round(max(-0.9,min(0.9,value))*32767)))
out=Path(__file__).resolve().parents[1]/'assets'/'record-shutter.wav'
with wave.open(str(out),'wb') as f:
    f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(b''.join(samples))
print(out)
