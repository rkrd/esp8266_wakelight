/* TODO
 * - Create time handling via calculation of deepsleep time etc.
 * - Sync time via internet
 * - Deepsleep
 *		+ Wake up every hour to check if it soon will be time to wake, adjust sleep intervall to match safe wake up delay.
 * - Disable deepsleep if reset is from reset button.
 * - Wake up from deepsleep from button or similar. Use reset-button.
 * - Extend setup mode/function.
 * Notes:
 * - rtc mem and ROM are not the same :)
 */

/* Look at this for persistent memory and maybe remove RTC module
 *  https://github.com/HarringayMakerSpace/IoT/tree/master/ESPDailyTask
 */

#include <time.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>

extern "C" {
#include <user_interface.h>
    extern struct rst_info resetInfo;
}


#include "wifi.h"

#define WAKEGRACE 60
#define DSLPTIME 60


bool restore_time(void);
bool save_times(void);
void set_time(String *req);

void handle_client(WiFiClient *client);
void set_alarm(String *req);
uint32_t calc32(const uint8_t *data, size_t length);

// Storage capacity is 512 bytes
struct {
    uint32_t crc32;
    byte data[508];
} rtc_data;

WiFiServer server(80);

bool wake_valid = false;
struct tm alarms[7];
time_t time_now;
time_t time_sleep;

void setup()
{
    // Debug
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

    restore_time();

    for (int i = 0; i < 7; i++) {
        Serial.print(alarms[i].tm_hour);
        Serial.print(":");
        Serial.println(alarms[i].tm_min);
    }

    struct tm *tnow = localtime(&time_now);
    int now_sec = tnow->tm_hour * 3600 + tnow->tm_min * 60 + tnow->tm_sec;
    int d = tnow->tm_wday;
    int alarm_sec = alarms[d].tm_hour * 3600 + alarms[d].tm_min + alarms[d].tm_sec;
    int diff = now_sec - alarm_sec;

    if (diff < WAKEGRACE) {
        // alarm
    }

    if (1 /*!goto_sleep*/) {
        WiFi.begin(ssid, password);
        Serial.println("Connecting to wifi");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        server.begin();
        Serial.println(WiFi.localIP());
    } else {
        time_sleep = diff < DSLPTIME ? diff - 10 : DSLPTIME;
        rtc_data.crc32 = calc32(((uint8_t*) &rtc_data) + 4, sizeof(rtc_data) - 4);
        ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtc_data, sizeof(rtc_data));
        Serial.println("Going to sleep");
        ESP.deepSleep(1000000 * time_sleep);
    }
}

void loop()
{
    WiFiClient client = server.available();
    Serial.println("Wait client");
    if (client) { 
        while (!client.available()) delay(1);
        Serial.println("Handle client");
        handle_client(&client);
    }

    Serial.println("Client done.");

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
    if (req.indexOf("/set/alarm") != -1) {
        set_alarm(&req);
    } else if (req.indexOf("/set/time/") != -1) {
        set_time(&req);
    } else {
        /*
           Serial.println("invalid request");
           client->stop();
           return;
         */
    }

    client->print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n \ 
        <!DOCTYPE HTML>\r\n<html>\r\n");
    char b[100];
    for (int i = 0; i < 7; i++) {
        snprintf(b, sizeof b, "Alarm[%d] -> %02d:%02d<br />\n",
            i, alarms[i].tm_hour, alarms[i].tm_min);
        client->print(b);
    }

    struct tm *t = localtime(&time_now);
    snprintf(b, sizeof b, "Time now %d-%d-%d %02d:%02d:%02d<br />\n",
        1900 + t->tm_year, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    client->print(b);
    snprintf(b, sizeof b, "Sleep duration %d<br />\n", time_sleep);

    client->print("</html>\n");

    delay(1);
}

void set_alarm(String *req)
{
    // Dummy set
    for (int i = 0; i < 7; i++) {
        alarms[i].tm_hour = 6;
        alarms[i].tm_min = 25;
        Serial.print("set alarm ");
    }
}

void set_time(String *req)
{
    String s = req->substring(strlen("/set/time/"));
    Serial.println(s);
    time_now = 1495044685;
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

bool restore_time(void)
{
    uint8_t sz_alarms = sizeof alarms;
    uint8_t sz_times = sizeof time_now;
    uint8_t *p = &rtc_data.data[0];


    if (!ESP.rtcUserMemoryRead(0, (uint32_t*) &rtc_data, sizeof rtc_data))
        return false;

    uint32_t crc = calc32(((uint8_t*) &rtc_data) + 4, sizeof rtc_data - 4);
    if (crc != rtc_data.crc32)
            return false;
    
    memcpy(&alarms, p, sz_alarms);
    p += sz_alarms;
    memcpy(&time_now, p, sz_times);
    p += sz_times;
    memcpy(&time_sleep, p, sz_times);

    time_now += time_sleep;

    return true;
}

bool save_times(void)
{
    rtc_data.crc32 = calc32(((uint8_t*) &rtc_data) + 4, sizeof(rtc_data) - 4);
    return ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtc_data, sizeof(rtc_data));
}
