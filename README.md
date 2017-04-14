# JMPXRDS
An FM MPX signal generator on top of Jack Audio Connection Kit with support for:
* Multiple SSB modulators for the stereo subchannel (L-R)
* RDS Encoder with support for all basic fields
* Built-in RTP server for sending the generated signal on a remote site

The generator output has a minimum sampling rate of 192000, if jack runs at 192000 or higher sampling rates an output port will also be registered, if not output will only be sent through the RTP server or a local unix socket (e.g. for use with netcat). Note that RTP server sends the signal FLAC-encoded to reduce bandwidth.
