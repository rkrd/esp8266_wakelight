/* TODO
 * - Create time handling via calculation of deepsleep time etc.
 * - Sync time via internet
 * - Deepsleep
 *		+ Wake up every hour to check if it soon will be time to wake, adjust sleep intervall to match safe wake up delay.
 * - Disable deepsleep if reset is from reset button.
 * - Wake up from deepsleep from button or similar. Use reset-button.
 * - Extend setup mode/function.
 * - system_get_rtc_time() // Check if usefull
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

#define RTC_USRMEM_START 64
#define MEM_ALIGN 4

extern "C" {
#include <user_interface.h>
const rst_info *rst_inf;
}
extern "C" void esp_yield();

#define DEBUG
#define MAIN_DBG

#include "wifi.h"
#define DSLPTIME (15 * 60)
#define DEBUGPIN 4 /* D2 */ /* D4 is lead on board */

#define NTP_PACKET_SIZE 48
#define SEVENTY_YEARS 2208988800UL

#define SYNC_CYCLES 1 /* Number of cycles to sleep before syncing time via NTP */


void init_mem(void);
void init_wifi(void);

bool save_times(void);
int restore_time(void);
time_t sleep_comp(time_t expected, time_t real);
uint32_t calc32(const uint8_t *data, size_t length);
void set_time(String *req);

void handle_client(WiFiClient *client);
void set_alarm(String *req);
time_t get_ntp_time(void);

void blink_debug(int);
void dweet(String);

void eeprom_save_alarm(uint8_t wday, struct alarm *a);
void eeprom_read_alarms();

// Storage capacity is 512 bytes
struct {
    uint32_t crc32;
    byte data[508];
} rtc_data;

struct alarm {
    uint8_t a_hour;
    uint8_t a_min;
} __attribute__ ((aligned (MEM_ALIGN)));

WiFiServer server(80);

struct alarm *alarms;
time_t *time_now;
time_t *time_sleep;
time_t *time_comp;
byte *sleep_cycles;

void setup()
{
    bool init = false;
    Serial.begin(9600);
    blink_debug(DEBUGPIN);

    Serial.print("Reset reason: ");
    rst_inf = system_get_rst_info();

    switch(rst_inf->reason) {
        case REASON_DEFAULT_RST: /* Power on */
            Serial.println("Power on");
            init = true;
            break;
        case REASON_DEEP_SLEEP_AWAKE:
            Serial.println("Deep-Sleep Wake");
            break;
        case REASON_EXT_SYS_RST:
            Serial.println("Reset");
            init = true;
            break;
        default:
            Serial.println("Other");
            break;
    }

    if (init) {
        init_mem(); 
    } else {
        int ret = restore_time();
        if (ret < 0) {
            Serial.print("Something went wrong, restart ");
            Serial.println(ret);
            system_restart(); /* Something failed, try with reset. */
        }
    }

    time_t tsave = *time_now;
    if (!init && !(*sleep_cycles % SYNC_CYCLES)) {
        *time_now = get_ntp_time();
        *time_comp = sleep_comp(tsave, *time_now);
    }
    dweet(String("restore_") + tsave + String("_ntp_") + *time_now + String("_compensation_") + *time_comp + String("_cycle_") + *sleep_cycles);

    *time_sleep = DSLPTIME;
    *sleep_cycles += 1;

#ifdef MAIN_DBG
    Serial.println("Going to sleep");
    Serial.println(String("Time to sleep:") + *time_sleep);
    Serial.println(String("Time nowe:") + *time_now);
    Serial.println(String("Time compensation:") + *time_comp);
    Serial.println(String("Sleep cycles:") + *sleep_cycles);
    Serial.println("Go to sleep");
#endif

    save_times();
    system_deep_sleep(1000000 * (*time_sleep + *time_comp));
    esp_yield();
}

void loop()
{
    static bool server_started = false;
    if (!server_started)
        server.begin();

    WiFiClient client = server.available();
    //Serial.println("Wait client");
    if (client) { 
        while (!client.available()) delay(1);
        Serial.println("Handle client");
        handle_client(&client);
    }

    //Serial.println("Client done.");

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
    } else if (req.indexOf("/sleep") != -1) {
        client->stop();
        server.stop();
        return;
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
                i, alarms[i].a_hour, alarms[i].a_min);
        client->print(b);
    }

    struct tm *t = localtime(time_now);
    snprintf(b, sizeof b, "Time now %d-%d-%d %02d:%02d:%02d<br />\n",
            1900 + t->tm_year, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    client->print(b);
    snprintf(b, sizeof b, "Sleep duration %d<br />\n", *time_sleep);

    client->print("</html>\n");

    delay(1);
}

// Shall handle to parse a string and set an alarm.
// Use something like:
// /set/alarm/<int>weekday/<int,int,int,int>HHMM
void set_alarm(String *req)
{
    if (!req) {
        // Dummy set
        for (int i = 0; i < 7; i++) {
            alarms[i].a_hour = 6;
            alarms[i].a_min = 25;
            Serial.print("set alarm ");
        }
    }
    int pos, wday, t;

    pos = req->indexOf("/set/alarm/");
    String s = req->substring(pos + strlen("/set/alarm/"));
    wday = s.toInt();

    if (wday < 0 && wday > 7)
        return;

    s = s.substring(2);
    t = s.toInt();

    alarms[wday].a_hour = t / 100;
    alarms[wday].a_min = t - (t / 100) * 100;

    eeprom_save_alarm(wday, alarms + wday);
    save_times();
}

// Setup current time 
// Format not sure...
// Will also add NTP which will render this one pretty useless
// But might be nice to not rely on internet connection
void set_time(String *req)
{
    String s = req->substring(strlen("/set/time/"));
    Serial.println(s);
    *time_now = 1495044685;
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

int restore_time(void)
{
    uint8_t sz_alarms = 7 * sizeof *alarms; // 7 alarms stored in struct tm
    uint8_t sz_times = sizeof *time_now;
    uint8_t *p;


    if (!system_rtc_mem_read(RTC_USRMEM_START + 0, (uint32_t*) &rtc_data, sizeof rtc_data))
        return -1;

    uint32_t crc = calc32(((uint8_t*) &rtc_data) + 4, sizeof rtc_data - 4);
    if (rtc_data.data[0] != 0x55 && 
        rtc_data.data[1] != 0x55 &&
        rtc_data.data[2] != 0x55 &&
        rtc_data.data[3] != 0x55) {
        rtc_data.crc32 = crc;
        memset(rtc_data.data, 0x55, 4);
    }

    if (crc != rtc_data.crc32)
        return -2;

    p = rtc_data.data;
    p += MEM_ALIGN;

    alarms = (struct alarm*)p;
    p += sz_alarms;

    time_now = (time_t*)p;
    p += sz_times;

    time_sleep = (time_t*)p;
    p += sz_times;

    time_comp = (time_t*)p;
    p += sz_times;

    sleep_cycles = p;

    *time_now += *time_sleep;


#ifdef DEBUG
    Serial.print("rtc_data.data[0]");
    Serial.println(rtc_data.data[0]);
    if (!rtc_data.data[0]) {
        /* RTC data not init. */
        *sleep_cycles = 0;
    }
    Serial.println("Restore time OK");
    Serial.println(*time_now);
    Serial.println(*time_sleep);
    Serial.println(*time_comp);
    for (int i = 0; i < 7; i++) {
        Serial.print(alarms[i].a_hour);
        Serial.print(":");
        Serial.println(alarms[i].a_min);
    }
#endif

    return 0;
}

bool save_times(void)
{
    rtc_data.crc32 = calc32(((uint8_t*) &rtc_data) + 4, sizeof(rtc_data) - 4);
    return system_rtc_mem_write(RTC_USRMEM_START + 0, (uint32_t*) &rtc_data, sizeof(rtc_data));
}

void blink_debug(int pin)
{
    Serial.println("blink_debug");

    digitalWrite(pin, HIGH);
    pinMode(pin, OUTPUT);
    pinMode(13, INPUT);
    digitalWrite(13, LOW);
    for (int i = 0; i < 5; ++i) {
        delay(100);
        digitalWrite(pin, i % 2);
        Serial.print(i%2);
    }

    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);
}

void dweet(String s)
{
    WiFiClient client;
    const char* host = "dweet.io"; 
    const int httpPort = 80;
    if (!client.connect(host, httpPort)) {
        Serial.println("connection failed");
        return;
    }

    // MYDWEET defined in wifi.h
    String url = String("/dweet/for/"MYDWEET"?msg=") + s;

    client.print(String("POST ") + url + " HTTP/1.1\r\n" +
            "Host: " + host + "\r\n" +
            "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000) {
            Serial.println(">>> Client Timeout !");
            client.stop();
            return;
        }
    }

    while(client.available()){
        String line = client.readStringUntil('\r');
        Serial.print(line);
    }
}

time_t get_ntp_time(void)
{
    const char* ntp_url = "time.nist.gov";
    unsigned int localPort = 2390;
    WiFiUDP udp;
    struct tm *ntp_tm;
    char str[100];
    byte packet[NTP_PACKET_SIZE];
    IPAddress ipaddr;
    time_t t = -1;


    if (WiFi.status() != WL_CONNECTED)
        init_wifi();

    udp.begin(localPort);

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

    int cb, i = 0;;
    while (cb = udp.parsePacket(), !cb) {
        Serial.println("no packet yet");
        Serial.println(cb);
        delay(500);
        udp.beginPacket(ipaddr, 123);
        udp.write(packet, NTP_PACKET_SIZE);
        udp.endPacket();
        yield();

        if (++i == 5)
            goto end;
    }

    Serial.print("packet received, length=");
    Serial.println(cb);
    udp.read(packet, NTP_PACKET_SIZE); // read the packet into the buffer

    t = packet[40] << 24 | packet[41] << 16 | packet[42] << 8 | packet[43];
    t -= SEVENTY_YEARS;

    ntp_tm = gmtime(&t);
    snprintf(str, 100, "%d-%d-%d %d:%d\n",
            ntp_tm->tm_year + 1900,
            ntp_tm->tm_mon,
            ntp_tm->tm_mday,
            ntp_tm->tm_hour,
            ntp_tm->tm_min);
    Serial.print(String("Time recieved: "));
    Serial.println(String(str));

end:
    udp.stop();
    return t;
}

void eeprom_save_alarm(uint8_t wday, struct alarm *a)
{
    uint8_t *p;
    size_t s = sizeof *a;

    EEPROM.begin(512);
    p = EEPROM.getDataPtr();

    if (!p)
        return;

    memcpy(p + (wday * s), a, s);
    EEPROM.commit();
    EEPROM.end();
}

void eeprom_read_alarms()
{
    uint8_t *p;
    size_t s = sizeof *alarms;

    EEPROM.begin(512);
    p = EEPROM.getDataPtr();

    if (!p)
        return;

    memcpy(alarms, p, 7 * s);

    /* Not the cleanest exit, but end() will commit() after data ptr is received.
       Hope it's ok ¯\_(ツ)_/¯ */

#ifdef DEBUG
    Serial.println("EEPROM saved alarms:");
    for (int i = 0; i < 7; ++i)
        Serial.println(String("alarm ") + alarms[i].a_hour + String(":") + alarms[i].a_min);
#endif
}

// Get time to compensate per sleep cycle
time_t sleep_comp(time_t expected, time_t real)
{
    time_t diff = expected - real;
    diff = diff / 3;
    diff = diff * 2;
    diff = diff / SYNC_CYCLES; 

    return diff;
}

void init_wifi(void)
{
    WiFi.begin(ssid, password);
    Serial.println("Connecting to wifi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(WiFi.localIP());
}

void init_mem(void)
{
    if (WiFi.status() != WL_CONNECTED)
        init_wifi();

    restore_time(); // Setup time pointers;
    eeprom_read_alarms();
    *time_comp = 0;
    *sleep_cycles = 0;
    *time_now = get_ntp_time();
}
