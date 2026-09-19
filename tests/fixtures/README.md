# Synthetic MPEG fixture

mpeg-test.mpg contains three seconds of generated test bars and a 440 Hz tone;
it contains no game assets. Recreate with FFmpeg:

```
ffmpeg -y -f lavfi -i testsrc=size=160x120:rate=25 -f lavfi -i sine=frequency=440:sample_rate=44100 -t 3 -c:v mpeg1video -bf 2 -c:a mp2 -ac 2 -f mpeg mpeg-test.mpg
```

Synthetic MP3 fixtures contain 1.1 seconds of generated sine tone, encoded using FFmpeg:

```
ffmpeg -f lavfi -i sine=frequency=440:duration=1.1 -ar 22050 -ac 1 -b:a 32k mp3-mpeg2-mono.mp3
ffmpeg -f lavfi -i sine=frequency=660:duration=1.1 -ar 44100 -ac 2 -b:a 128k mp3-mpeg1-stereo.mp3
ffmpeg -f lavfi -i sine=frequency=880:duration=1.1 -ar 11025 -ac 1 -b:a 16k mp3-mpeg25-mono.mp3
```
They contain no game audio.

mp3-long60.mp3 uses the same synthetic 440 Hz command with duration=60, 22050 Hz mono, 32k bitrate.
