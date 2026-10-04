The horizontal-plane impulse responses embedded in `kemarHorizontal.h` are from the
diffuse-field equalized KEMAR dataset by **Bill Gardner and Keith Martin**.
**Copyright 1994 MIT Media Laboratory.** The authors provide the data free with no
restrictions on use provided they are cited in research or commercial applications.

Source: https://sound.media.mit.edu/resources/KEMAR.html
Archive: http://sound.media.mit.edu/resources/KEMAR/diffuse.zip
Documentation: https://sound.media.mit.edu/resources/KEMAR/hrtfdoc.txt
FAQ: https://sound.media.mit.edu/resources/KEMAR/KEMAR-FAQ.txt
Source inspected: 2026-10-04.
Archive SHA256: a48e73137390626ef1e45d1de6a4d98530c6aa570e71b40f7c4442241ed5fa22.

Subset: elevation zero, azimuth 0 through 180 degrees in 5-degree increments;
37 WAV files, each 44100 Hz, 16-bit little-endian PCM, two interleaved channels
(left then right), 128 frames. This is the WAV distribution; the original `.dat`
distribution's big-endian description does not apply to these files.
The other hemisphere is obtained by swapping ears and mirroring azimuth.
No per-ear alignment, scaling, or normalization has been applied to the source table.
`scripts/buildKemarTable.ps1` rebuilds the integer table from the specified archive.

Orbit resamples each HRIR with a finite windowed-sinc low-pass kernel on a common
time origin, including the kernel's pre/post support. A shared added delay makes
the kernel causal, and a 1/sample-rate-ratio factor preserves impulse-response
coefficient gain. Dry audio receives this added delay. Original interaural delays
and direction-dependent spectral responses are retained subject to finite resampling
accuracy. The dataset is not personalized; measured front/back differences do not
guarantee every listener can perceptually distinguish front and back.
