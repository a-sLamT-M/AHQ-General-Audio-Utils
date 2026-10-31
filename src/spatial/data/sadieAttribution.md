# SADIE II elevation responses

Copyright 2018 University of York. Recorded and developed by Cal Armstrong,
Lewis Thresh and Gavin Kearney, AudioLab, University of York.
Distributed under Apache License 2.0; the complete license is in
[sadieLicense.txt](sadieLicense.txt).

Source: [SADIE II Database V2-1](https://zenodo.org/records/10886409),
published 2024-03-27, retrieved 2026-10-20.
The downloaded `D1_HRIR_SOFA.zip` matches the publisher's MD5
`4850c1eb8e63e2d4f605edcdb4d5c883`.
Extracted file: `D1_48K_24bit_256tap_FIR_SOFA.sofa`, SHA-256
`9af7cb19531e52fb7ae8ec92621e6ab62b1d5fe584b3742be36699a0ddb0ccd4`.
Its embedded license explicitly identifies Copyright 2018 University of York
and Apache License 2.0.

Changes: retained fourteen elevations at azimuth zero from the D1 KU100
diffuse-field-equalized, low-frequency-extended, 256-tap, 48 kHz responses;
converted coefficients to float32. No amplitude normalization was applied.
The generator is [buildSadieTable.py](../../../scripts/buildSadieTable.py).
Builds use the checked-in table and do not require Python, h5py or the full dataset.

The source's -90 degree response is an approximation interpolated by its authors
from measurements at -81 degrees. It is not a direct measurement below the chair.
These are general dummy-head responses, not measurements of the current listener.
Perceived elevation depends on the listener, headphones and source spectrum.

Reference: Armstrong, Thresh, Murphy and Kearney, “A Perceptual Evaluation of
Individual and Non-Individual HRTFs: A Case Study of the SADIE II Database,”
Applied Sciences 8(11), 2029 (2018), [doi:10.3390/app8112029](https://doi.org/10.3390/app8112029).
