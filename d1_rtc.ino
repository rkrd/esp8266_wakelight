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
#include <TimeLib.h>
#include <Time.h>
#include <DS1302RTC.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>

extern "C" {
#include <user_interface.h>
extern struct rst_info resetInfo;
}


#include "wifi.h"

// DS1302 drive pins
#define DS1302_GND_PIN 13
#define DS1302_VCC_PIN 12

struct {
	uint32_t crc32;
	byte data[508];
} rtc_data;

// Set pins:  CE, IO,CLK
DS1302RTC RTC(0, 2, 15);

WiFiServer server(80);

bool wake_valid = false;
uint8_t wake_hour[7];
uint8_t wake_minute[7];

void setup()
{
	Serial.begin(9600);
	
	Serial.print("Reset reason: ");

    if (resetInfo.reason == REASON_DEEP_SLEEP_AWAKE) {
	  Serial.println("Deep-Sleep Wake");
	} else if (resetInfo.reason == REASON_EXT_SYS_RST) {
	  Serial.println("Reset");
	} else {
	  Serial.println("Other");
	}

	Serial.println(resetInfo.reason);

	if (ESP.rtcUserMemoryRead(0, (uint32_t*) &rtc_data, sizeof(rtc_data))) {
		uint32_t crc = calc32(((uint8_t*) &rtc_data) + 4, sizeof(rtc_data) - 4);
		if (crc != rtc_data.crc32) {
			// Handle crc failure
		}
	} else {
		// RTC mem failed try to restore from ROM
		read_mem();
	}
	// DEBUG
	read_mem();
	for (int i = 0; i < 7; i++) {
		Serial.print(wake_hour[i]);
		Serial.print(":");
		Serial.println(wake_minute[i]);

	}

	Serial.println("DS1302RTC Read Test");
	Serial.println("-------------------");

	digitalWrite(4, HIGH);
	pinMode(4, OUTPUT);

	digitalWrite(DS1302_GND_PIN, LOW);
	digitalWrite(DS1302_VCC_PIN, HIGH);
	pinMode(DS1302_GND_PIN, OUTPUT);
	pinMode(DS1302_VCC_PIN, OUTPUT);

	Serial.println("RTC module activated");
	Serial.println();
	delay(500);

	if (RTC.haltRTC()) {
		Serial.println("The DS1302 is stopped.  Please run the SetTime");
		Serial.println("example to initialize the time and begin running.");
		Serial.println();
		set_time(2016, 1, 1, 12, 3, 4); // DEBUG
	}
	if (!RTC.writeEN()) {
		Serial.println("The DS1302 is write protected. This normal.");
		Serial.println();
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
		ESP.deepSleep(5e6);
	}


}

void loop()
{
	tmElements_t tm;

	WiFiClient client = server.available();
	if (client && client.available()) { 
		handle_client(&client);
	}

	Serial.print("UNIX Time: ");
	Serial.print(RTC.get());


	if (! RTC.read(tm)) {
		char *fmt = "Time = %02d:%02d:%02d %d-%02d-%02d\n";
		char s[30];
		snprintf(s, sizeof(s) - 1, fmt, tm.Hour, tm.Minute, tm.Second, 
				tmYearToCalendar(tm.Year), tm.Month, tm.Day);

		Serial.println(s);
	} else {
		Serial.println("DS1302 read error!  Please check the circuitry.");
		Serial.println();
		delay(9000);
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
	tmElements_t tm;
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
	if (!RTC.read(tm)) {
		char *fmt = "Time = %02d:%02d:%02d %d-%02d-%02d\n";
		char s[30];
		snprintf(s, sizeof(s) - 1, fmt, tm.Hour, tm.Minute, tm.Second, 
				tmYearToCalendar(tm.Year), tm.Month, tm.Day);

		Serial.println(s);
		client->print(s);

		for (int i = 0; i < sizeof wake_hour; i++) {
			snprintf(s, sizeof(s) - 1, "%d wake %02d:%02d\n", i, wake_hour[i], wake_minute[i]);
			client->print("<br />");
			client->print(s);
		}

		client->print(wake_valid ? "<br />valid" : "<br />invalid");

	} else {
		Serial.println("DS1302 read error!  Please check the circuitry.");
		Serial.println();
		client->print("DS1302 read error! Check circuitry.\n");
	}	client->print("</html>\n");

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

uint8_t set_time(uint16_t _y, uint8_t _m, uint8_t _d, uint8_t _H, uint8_t _M, uint8_t S)
{
	time_t t;
	tmElements_t tm;
	tm.Year = CalendarYrToTm(_y);
	tm.Month = _m;
	tm.Day = _d;
	tm.Hour = _H;
	tm.Minute = _M;
	tm.Second = _S;
	t = makeTime(tm);

	return RTC.set(t);
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
