CC=gcc
TARGET=jmpxrds_rtp_client
SOURCES=rtp_client.c

all:
	$(CC) -o $(TARGET) $(SOURCES) `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtp-1.0`

clean:
	rm -f $(TARGET)

