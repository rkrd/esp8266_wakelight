/* TODO
 * - Sync time via internet
 * - Deepsleep
 *		+ Wake up every hour to check if it soon will be time to wake, adjust sleep intervall to match safe wake up delay.
 * - Disable deepsleep if reset is from reset button.
 * - Wake up from deepsleep from button or similar. Use reset-button.
 * - Extend setup mode/function.
 * - rtc mem and ROM are not the same :)
 */

/* Look at this for persistent memory and maybe remove RTC module
 *  https://github.com/HarringayMakerSpace/IoT/tree/master/ESPDailyTask
 */
#include <ESP8266WiFi.h>
#include <EEPROM.h>

extern "C" {
#include <user_interface.h>
    extern struct rst_info resetInfo;
}


#include "wifi.h"

void read_mem(void);
uint8_t set_time(uint16_t _y, uint8_t _m, uint8_t _d, uint8_t _H, uint8_t _M, uint8_t S);
void handle_client(WiFiClient *client);
void set_alarm(String *req);
bool write_mem(uint8_t d, uint8_t h, uint8_t m);
void update_crc(void);
uint32_t calc32(const uint8_t *data, size_t length);

// Storage capacity is 512 bytes
struct {
    uint32_t crc32;
    byte data[508];
} rtc_data;

WiFiServer server(80);

bool wake_valid = false;
uint8_t wake_hour[7];
uint8_t wake_minute[7];

void setup()
{
    digitalWrite(4, HIGH);
    pinMode(4, OUTPUT);

    Serial.begin(9600);

    Serial.print("Reset reason: ");

    switch(resetInfo.reason) {
    case REASON_DEEP_SLEEP_AWAKE:
        Serial.println("Deep-Sleep Wake");
        break;
    case REASON_EXT_SYS_RST:
        Serial.println("Reset");
        break;
    default:
        Serial.println("Other");
        break;
    }

    Serial.println(resetInfo.reason);

    if (ESP.rtcUserMemoryRead(0, (uint32_t*) &rtc_data, sizeof(rtc_data))) {
        uint32_t crc = calc32(((uint8_t*) &rtc_data) + 4, sizeof(rtc_data) - 4);
        if (crc != rtc_data.crc32) {
            // Handle crc failure
        }
    } else {
        read_mem();
    }
    // DEBUG
    read_mem();
    for (int i = 0; i < 7; i++) {
        Serial.print(wake_hour[i]);
        Serial.print(":");
        Serial.println(wake_minute[i]);

    }

    if (0 /*!goto_sleep*/) {
        WiFi.begin(ssid, password);
        Serial.println("Connecting to wifi");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        server.begin();
        Serial.println(WiFi.localIP());
    } else {
        rtc_data.crc32 = calc32(((uint8_t*) &rtc_data) + 4, sizeof(rtc_data) - 4);
        ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtc_data, sizeof(rtc_data));
        delay(10000);
        Serial.println("Going to sleep");
        ESP.deepSleep(5e6);
    }


}

void loop()
{
    WiFiClient client = server.available();
    if (client && client.available()) { 
        handle_client(&client);
    }

    delay(1000);
}

void print2digits(int number)
{
    if (number >= 0 && number < 10)
        Serial.write('0');
    Serial.print(number);
}

void handle_client(WiFiClient *client)
{
    String req = client->readStringUntil('\r');
    client->flush();

    Serial.println(req);
    if (req.indexOf("/set") != -1) {
        set_alarm(&req);
    } else {
        /*
           Serial.println("invalid request");
           client->stop();
           return;
         */
    }

    client->print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n");
    client->print("</html>\n");

    delay(1);
}

void set_alarm(String *req)
{
    // Dummy set
    for (int i = 0; i < 7; i++) {
        wake_hour[i] = 6;
        wake_minute[i] = 25;
        bool b = write_mem(i, 6, 25);
        Serial.print("write ");
        Serial.println(b);
    }
    update_crc();
}

#define TIME_CRC 14 

void read_mem(void)
{
    EEPROM.begin(512);
    uint8_t h = 0;
    uint8_t m = 0;
    uint8_t chk;
    uint8_t cc = 0xee;

    for (int i = 0; i < 7; i++) {
        h = EEPROM.read(i);
        m = EEPROM.read(i + 7);

        cc ^= h ^ m;

        if (h < 24 && m < 60) {
            wake_hour[i] = h;
            wake_minute[i] = m;
        }
    } 

    chk = EEPROM.read(TIME_CRC);

    wake_valid = (cc == chk);
    Serial.print("read_mem() chksum ");
    Serial.print(cc);
    Serial.print(" ");
    Serial.println(chk);

    EEPROM.end();
}

bool write_mem(uint8_t d, uint8_t h, uint8_t m)
{
    uint8_t eh;
    uint8_t em;

    EEPROM.begin(512);
    if (d < 0 && d > 7)
        return false;
    if (h > 23)
        return false;
    if (m > 59)
        return false;

    EEPROM.write(d, h);
    EEPROM.write(d + 7, m);

    eh = EEPROM.read(d);
    em = EEPROM.read(d+7);

    EEPROM.commit();
    EEPROM.end();
    return (h == eh) && (m == em);
}

void update_crc(void)
{
    uint8_t c = 0xee;
    uint8_t v;

    EEPROM.begin(512);
    for (int i = 0; i < 14; i++) {
        v = EEPROM.read(i);
        c ^= v;
    }

    Serial.print("update_crc() chksum ");
    Serial.println(c);

    EEPROM.write(TIME_CRC, c);
    EEPROM.commit();
    EEPROM.end();
}

uint32_t calc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffff;
    while (length--) {
        uint8_t c = *data++;
        for (uint32_t i = 0x80; i > 0; i >>= 1) {
            bool bit = crc & 0x80000000;
            if (c & i) {
                bit = !bit;
            }
            crc <<= 1;
            if (bit) {
                crc ^= 0x04c11db7;
            }
        }
    }
    return crc;
}
