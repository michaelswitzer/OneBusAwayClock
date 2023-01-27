// -------------------------------------------------------------------------
// OneBusAway transit display using ESP8266 WiFi microcontroller and Adafruit
// 7-segment displays. 
//
// Based heavily on TinyXML example code for NextBus transit clock implementation:
// https://github.com/adafruit/TinyXML/tree/master/examples/NextBus
//
// This code has the exact same function, modified to interpret OBA API instead of NextBus.
// For more info go to:
// https://learn.adafruit.com/personalized-esp8266-transit-clock
// -------------------------------------------------------------------------

// CONFIGURABLE GLOBAL STUFF -----------------------------------------------
#include "config.h"

// REQUIRED LIBRARIES ------------------------------------------------------
#include <ESP8266WiFi.h>
#include <TinyXML.h>
#include <Wire.h>
#include <Adafruit_LEDBackpack.h>

// GLOBAL DATA -------------------------------------------------------------
char ssid[]    = WIFI_NAME,       // WiFi network name
     pass[]    = WIFI_PASS,       // WiFi network password
     host[]    = OBA_ENDPOINT,    // OneBusAway root API endpoint
     api_key[] = OBA_API_KEY;  // OneBusAway API key

struct {
  const uint8_t     addr;          // I2C address of display
  const char       *stopID;
  uint32_t          lastQueryTime; // Time of last query (millis())
  uint32_t          seconds[2];    // Most recent predictions from server
  Adafruit_7segment display;       // 7segment initialized at runtime
} stops[] = {
  { 0x70, BUS_STOP_TL }, // Top Left
  { 0x71, BUS_STOP_BL }, // Bottom Left
  { 0x72, BUS_STOP_TR }, // Top Right
  { 0x73, BUS_STOP_BR }  // Bottom Right
};

#define NUM_STOPS (sizeof(stops) / sizeof(stops[0]))

WiFiClient client;
TinyXML    xml;
uint8_t    buffer[150]; // For XML decoding
uint8_t    s=NUM_STOPS; // Stop # currently being searched
uint32_t   lastConnectTime = -(POLL_INTERVAL * 60000L); // neg on purpose!
uint32_t   seconds[2];
uint32_t   currentTime;
char       truncTime[10]; // need to truncate milliseconds off of epoch time to fit in uint32
int        predict;

// UTILITY FUNCTIONS -------------------------------------------------------
void XML_callback(uint8_t statusflags, char* tagName,
 uint16_t tagNameLen, char* data, uint16_t dataLen) {
  if(data){ // If we get data...  
    if(strstr(tagName, "currentTime")) { // If data is the current time
      strncpy (truncTime, data, 10);
      currentTime = atoi(truncTime);
      Serial.print("currentTime: "); Serial.println(currentTime);
    }
    
    else if(strstr(tagName, "scheduledDepartureTime")) { // If data is a scheduled departure time
      strncpy (truncTime, data, 10);
      predict = atoi(truncTime) - currentTime; // Prediction in seconds (0 if gibberish)
      Serial.print("scheduled: "); Serial.println(predict / 60);
    }
    
    else if(strstr(tagName, "predictedDepartureTime")) { // If data is a predicted departure time
      if (atoi(data) != 0) {
        strncpy (truncTime, data, 10);
        predict = atoi(truncTime) - currentTime; // Prediction in seconds (0 if gibberish)
        Serial.print("predicted: "); Serial.println(predict / 60);
      } else {
        Serial.println("no prediction");
      }
      
     if(predict >= (MIN_TIME * 60)) {        // Above our "too soon" threshold?
      if(!seconds[0]) {               //  No predictions yet?
        seconds[0] = predict;               //   Save in slot 0, done
      } else {                        //  Else 1-2 existing predictions...
        if(predict <= seconds[0]) {         // New time sooner than slot 0?
          seconds[1] = seconds[0];    //  Move 0 up to 1 (old 1 discarded)
          seconds[0] = predict;             //  Store new time in 0
        } else if(!seconds[1] ||      // Slot 1 empty?
                 (predict <= seconds[1])) { // Or new time sooner than 1?
          seconds[1] = predict;             //  Store new time in slot 1
        }                             // Else discard
        if(seconds[0] == seconds[1]) seconds[1] = 0; // If equal, delete 1
     }
    }
   }     
  }
}

// Update 7-segment I2C display(s) with arrival predictions
void refresh(void) {
  uint32_t t, dt;
  int16_t  p[2], m; // Prediction times (in minutes)
  uint8_t  i, j, k, idx[] = { 0, 1, 3, 4 }; // 7-segment digit indices

  yield(); // Handle WiFi events

  for(i=0; i<NUM_STOPS; i++) { // For each stop...
    // Time difference (seconds) since last query
    dt = ((t = millis()) - stops[i].lastQueryTime + 500) / 1000;
    // Convert predictions (stops[i].seconds, up to 2, sorted) to minutes
    memset(p, 0, sizeof(p));       // Assume nothing
    for(j=k=0; j<2; j++) {         // For each possible prediction...
      if(stops[i].seconds[j]) {    // Prediction available?
        int32_t s = (stops[i].seconds[j] - dt); // Possibly negative
        if(s >= (MIN_TIME * 60)) { // Above min time threshold?
          m = s / 60;              // Seconds -> minutes
          if(m > 99) m = 99;       // Clip to 2 digits
          p[k++] = m;              // Store
        }
      }
    }

    if(!k) {
      // No current predictions for this stop. Display '----'
      for(j=0; j<4; j++) {
        stops[i].display.writeDigitRaw(idx[j], 0b01000000);
      }
    } else {
      stops[i].display.clear();
      // If two predictions for this stop, and if earlier prediction
      // is 10 minutes or more, don't show the second prediction (it
      // won't fit on display). Two predictions are shown ONLY if
      // both will fit with a blank digit between them.
      if(p[0] >= 10) p[1] = 0;

      if(p[1]) {
        stops[i].display.print(p[1]); // 2nd prediction on right
        stops[i].display.writeDigitNum(0, p[0], false); // 1st on left
      } else {
        // Single prediction sorta left-ish justified on digits 0 & 1
        if(p[0] >= 10) stops[i].display.writeDigitNum(0, p[0] / 10, false);
        stops[i].display.writeDigitNum(1, p[0] % 10, false);
      }
    }

    stops[i].display.writeDisplay();

    // If this stop is currently being searched, pulse brightness
    if(i == s) {
      j = t >> 6; // ~1/16 sec
      if(j & 0x10) stops[i].display.setBrightness(      j & 0xF);
      else         stops[i].display.setBrightness(15 - (j & 0xF));
    } else         stops[i].display.setBrightness(k ? 15 : 1);
  }
}

// ONE-TIME INITIALIZATION -------------------------------------------------

void setup(void) {
  Serial.begin(115200);
  delay(1000); // Slight pause to allow serial to begin
  Serial.println("OneBusAway Tracker");
  Wire.begin();
  xml.init((uint8_t *)buffer, sizeof(buffer), &XML_callback);

  for(uint8_t i=0; i<NUM_STOPS; i++) {
    stops[i].display.begin(stops[i].addr);
    stops[i].display.clear();
    stops[i].display.writeDisplay();
    stops[i].lastQueryTime = 0;
    memset(stops[i].seconds, 0, sizeof(stops[i].seconds));
  }
}

// MAIN LOOP ---------------------------------------------------------------

void loop(void) {
  uint32_t t;
  int      c;
  uint8_t  b = 0;
  boolean  timedOut;

  while(((t = millis()) - lastConnectTime) < (POLL_INTERVAL * 60000L)) {
    refresh();
  }
  lastConnectTime = t;

  if(WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi connecting..");
    WiFi.begin(ssid, pass);
    // Wait up to 1 minute for connection...
    for(c=0; (c < 60) && (WiFi.status() != WL_CONNECTED); c++) {
      Serial.write('.');
      for(t = millis(); (millis() - t) < 1000; refresh());
    }
    if(c >= 60) { // If it didn't connect within 1 min
      Serial.println("Failed. Will retry...");
      return;
    }
    Serial.println("OK!");
    delay(10000); // Pause before hitting it with queries & stuff
  }

  for(s=0; s<NUM_STOPS; s++) {
    Serial.print("Stop #");
    Serial.println(stops[s].stopID);
    Serial.print("Contacting server...");
    if(client.connect(host, 80)) {
      Serial.println("OK\r\nRequesting data...");
      client.print("GET /api/where/arrivals-and-departures-for-stop/");
      client.print(stops[s].stopID);
      client.print(".xml?key=");
      client.print(api_key);
      client.print(" HTTP/1.1\r\nHost: ");
      client.print(host);
      client.print("\r\nConnection: Close\r\n\r\n");
      client.flush();
      xml.reset();
      memset(seconds, 0, sizeof(seconds)); // Clear predictions
      t        = millis(); // Start time
      timedOut = false;
      while(client.connected()) {
        if(!(b++ & 0x40)) refresh(); // Every 64 bytes, handle displays
        if((c = client.read()) >= 0) {
          xml.processChar(c);
          t = millis(); // Reset timeout clock
        } else if((millis() - t) >= (READ_TIMEOUT * 1000)) {
          Serial.println("---Timeout---");
          timedOut = true;
          break;
        }
      }
      if(!timedOut && seconds[0]) { // Successful transfer?
        // Copy newly-polled predictions to stops structure:   
        memcpy(stops[s].seconds, seconds, sizeof(seconds));
        stops[s].lastQueryTime = millis();
      }
    }
    client.stop();
    Serial.println();
  }
}

