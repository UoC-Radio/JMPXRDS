CC=gcc
TARGET=jmpxrds_rtp_client
SOURCES=rtp_client.c

all:
	$(CC) -o $(TARGET) $(SOURCES) `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtp-1.0`

install:
	install -m 755 $(TARGET) /usr/local/bin
	install -m 644 $(TARGET).env /etc/default
	install -m 644 $(TARGET).service /etc/systemd/system

enable:
	systemctl daemon-reload
	systemctl enable $(TARGET).service

clean:
	rm -f $(TARGET)
