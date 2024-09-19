# ProRes image decoder

Although ProRes is actually a video format, each individual frame can be considered an image in it's own right.

To convert an image to ProRes storage, run this FFmpeg command

```
ffmpeg -i <source> -c:v prores_ks -profile:v <profile> -f image2 out.prores
```

Profiles (from lowest to highest)

| profile name | format tag | chroma subsampling | bitdepth | max bpp @ 1920x1080 | quality |
| - | - | - | - | - | - |
| proxy | apco | 4:2:2 | 10bpc | 0.72 bpp (27.8:1 ratio) | ok |
| lt | apcs | 4:2:2 | 10bpc | 1.64 bpp (12.2:1 ratio) | decent |
| standard | apcn | 4:2:2 | 10bpc | 2.35 bpp (8.5:1 ratio) | good |
| hq | apch | 4:2:2 | 10bpc | 3.54 bpp (5.6:1 ratio) | great |
| 4444 | ap4h | 4:4:4 | 12bpc | 5.30 bpp (6.8:1 ratio) | great |
| 4444xq | ap4x | 4:4:4 | 12bpc | 7.96 bpp (4.5:1 ratio) | visually lossless |