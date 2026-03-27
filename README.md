# esp_audio_dac_stream
ESP-ADF output stream driver for build-in DAC

ESP-ADF i2s component does not support build-in DAC anymore.
Use this component in audio pipeline to output audio using internal DAC.

DAC is connected to GPIO 25 (left) and GPIO 26 (right) in esp32.

Internal DAC audio is REALLY BAD. Use this component for experiments only.
You can buy a I2S amplifier module for a couple of euros, so it is not a cost issue.

You need to dowload esp-adf first. e.g.
```
git clone https://github.com/espressif/esp-adf.git
export ADF_PATH=$(pwd)/esp-adf

git clone https://github.com/kasbert/esp_audio_dac_stream
cd esp_audio_dac_stream/examples/play_mp3_control
idf.py update-dependencies
idf.py build
idf.py -p /dev/ttyUSB0 build flash monitor
```