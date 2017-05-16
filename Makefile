ESP_ROOT=/opt/arduino/hardware/esp8266com/esp8266
USER_INC = $(ARDUINO_BASE)/libs/Time-master
include $(ARDUINO_BASE)/tools/makeEspArduino/makeEspArduino.mk

SKETCH = d1_rtc.ino

#UPLOAD_PORT = /dev/ttyUSB1
#BOARD = esp210
