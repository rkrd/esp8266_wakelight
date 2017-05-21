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
#include <WiFiUdp.h>


extern "C" {
#include <user_interface.h>
    extern struct rst_info resetInfo;
}


#include "wifi.h"

#define WAKEGRACE 60
#define DSLPTIME (60 * 60)

#define NTP_PACKET_SIZE 48
#define SEVENTY_YEARS 2208988800UL



bool restore_time(void);
bool save_times(void);
void set_time(String *req);

void handle_client(WiFiClient *client);
void set_alarm(String *req);
uint32_t calc32(const uint8_t *data, size_t length);
time_t get_ntp_time(void);


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

    // Check if supposed to go to sleep or handle wifi connection.
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
        // Deep sleep for configured time if time until next alarm is bigger than
        // configured time. Otherwise sleep a bit but wake up in time for alarm.
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

// Shall handle to parse a string and set an alarm.
// Use something like:
// /set/alarm/<int>weekday/<int,int,int,int>HHMM
void set_alarm(String *req)
{
    // Dummy set
    for (int i = 0; i < 7; i++) {
        alarms[i].tm_hour = 6;
        alarms[i].tm_min = 25;
        Serial.print("set alarm ");
    }
}

// Setup current time 
// Format not sure...
// Will also add NTP which will render this one pretty useless
// But might be nice to not rely on internet connection
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
time_t get_ntp_time(void)
{
	WiFiUDP udp;
	unsigned int localPort = 2390;
	udp.begin(localPort);

	IPAddress ipaddr;
	const char* ntp_url = "time.nist.gov";


	byte packet[NTP_PACKET_SIZE];

	WiFi.hostByName(ntp_url, ipaddr);

	Serial.println(String("Server IP ") + ipaddr);
	Serial.println("sending NTP packet...");

	memset(packet, 0, NTP_PACKET_SIZE);

	packet[0] = 0b11100011;   // LI, Version, Mode
	packet[1] = 0;     // Stratum, or type of clock
	packet[2] = 6;     // Polling Interval
	packet[3] = 0xEC;  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packet[12]  = 49;
	packet[13]  = 0x4E;
	packet[14]  = 49;
	packet[15]  = 52;

	udp.beginPacket(ipaddr, 123);
	udp.write(packet, NTP_PACKET_SIZE);
	udp.endPacket();
	yield();

	int cb;
	while (cb = udp.parsePacket(), !cb) {
		Serial.println("no packet yet");
		Serial.println(cb);
		delay(500);
		udp.beginPacket(ipaddr, 123);
		udp.write(packet, NTP_PACKET_SIZE);
		udp.endPacket();
		yield();
	}

	Serial.print("packet received, length=");
	Serial.println(cb);
	udp.read(packet, NTP_PACKET_SIZE); // read the packet into the buffer

	time_t t = packet[40] << 24 | packet[41] << 16 | packet[42] << 8 | packet[43];
	t -= SEVENTY_YEARS;

	struct tm *ntp_tm = gmtime(&t);
	char str[100];
	snprintf(str, 100, "%d-%d-%d %d:%d\n",
		ntp_tm->tm_year + 1900,
		ntp_tm->tm_mon,
		ntp_tm->tm_mday,
		ntp_tm->tm_hour,
		ntp_tm->tm_min);
	Serial.print(String("Time recieved: "));
	Serial.println(String(str));

	return t;
}
