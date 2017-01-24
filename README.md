# Sleep timer concept

Wifi configuration is stored in file wifi.h
```C
const char *ssid = "ssid";
const char *password = "psk";
```

D2 (gpio16) must be connected to RESET via 10K ohm for wake to function. If resistor is not used programming will fail when connected.
D4 (gpio4) is used for a debug LED.

