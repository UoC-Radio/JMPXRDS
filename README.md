# JMPXRDS [![Travis (.org)](https://img.shields.io/travis/uoc-radio/jmpxrds)](https://travis-ci.org/github/UoC-Radio/JMPXRDS/) [![Sonar Quality Gate](https://img.shields.io/sonar/quality_gate/UoC-Radio_JMPXRDS?server=https%3A%2F%2Fsonarcloud.io)](https://sonarcloud.io/dashboard?id=UoC-Radio_JMPXRDS) [![Sonar Coverage](https://img.shields.io/sonar/coverage/UoC-Radio_JMPXRDS?server=https%3A%2F%2Fsonarcloud.io)](https://sonarcloud.io/component_measures?id=UoC-Radio_JMPXRDS&metric=coverage)
An FM MPX signal generator on top of Jack Audio Connection Kit with support for:
* Typical DSB modulator
* Multiple SSB modulators for the stereo subchannel (L-R)
* RDS Encoder with support for all basic fields
* Built-in RTP server for sending the generated signal on a remote site

The generator output has a fixed sampling rate of 192000, it outputs data to a local unix socket (for the GUI to do FFT analysis and/or other uses e.g. netcat/sox), and through a built-in RTP server. Note that RTP server sends the signal FLAC-encoded to reduce bandwidth. A client for the RTP server is also available as well as a GNU Radio - based receiver for debugging. The signal can be used to drive an FM exciter via a normal sound card. We 've tried this with RPi + HiFi Berry and Odroid + HiFi Shield.

The optional GUI is based on GTK3+ and can be used to fully control the generator, alternatively a set of command line tools are also available for headless systems. The generator itself is a standalone app and uses shared memory to communicate with the GUI/tools.

It's currently used in production on 3 radio stations in Crete (UoC Radio 96.7, MatzoRe 89.1, Best 94.7). In order to operate this properly you should also use some audio processing, we recommend [Calf Studio Gear](https://calf-studio-gear.org/). The default settings together with a properly configured compressor/limiter will get you within the allowed deviation constraints (75KHz). You should also make sure your exciter and amplifier are properly set up.


An overview of JMPXRDS is included in a presentation at [FOSDEM 2018](https://youtu.be/H6Ki-RbeSHI?t=603).
