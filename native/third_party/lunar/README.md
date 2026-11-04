# Embedded almanac data

Source: [6tail/lunar-python](https://github.com/6tail/lunar-python), version 1.4.8,
MIT License, Copyright (c) 2020 6tail. See [LICENSE](LICENSE).

`almanac_data.h` embeds the original daily activity table and China civil dates
of the 24 solar terms for 1900–2100. WinCal uses the library's default daily
almanac convention (sect 1): solar-term months change on the civil date of the
term. Almanac traditions and other sources may use different conventions.

Runtime is entirely native and offline; Python is only needed to regenerate:

```powershell
python -m pip install lunar_python==1.4.8
python native/tools/generate-almanac.py
```

The generator also writes reference results in `tests/almanac_fixtures.h`.
The license is installed beside the application in `licenses/lunar-LICENSE.txt`.
