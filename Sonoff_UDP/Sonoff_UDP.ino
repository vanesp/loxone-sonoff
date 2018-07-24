
/*
 * Door controller for Loxone
 * 
 * This program is for a Sonoff SV ESP 8266 controller,
 * These control door RELAY / door close contacts
 * 
 * Documentation:
 * https://www.itead.cc/wiki/Sonoff_SV
 * 
 * Configure Arduino IDE as follows
 * https://github.com/arendst/Sonoff-Tasmota/wiki/Arduino-IDE
 * To program: 
 * 
 * Hold down the reset button, then insert USB cable to USB BUBII
 * Then release RESET button
 * pin 12 is connected to Relay 
 * 
 * pin 4 (IO4) is a movement sensor. It is used to keep track of the state of the door or gate
 * On the gate with the Beninca HEADY controller this sensor works as follows:
 * One contact is connected to ground
 * One contact is connected to the input senso 
 * SCA - gate closed, contact open - reads HIGH
 * gate open - contact closed - reads LOW
 * gate moving, contact flashing...
 * 
 * Control is via sending UDP commands to port 2807 of the IP address (which is linked to the MAC address and static)
 * 
 * UDP commands are:
 * open (to open the gate)
 * close (to close the gate)
 * 
 */

// Note: Sonoff SV MAC address: 68:C6:3A:95:97:05


#define DEBUG 1
#define GATE  1      // or GARAGE

#include <ESP8266WiFi.h>..
#include <WiFiUdp.h>

#include "config.h"   // details of our network
// contains defines of 
// const char* ssid 
// const char* passwd
// look at config-sample.h for an example and edit and save as config.h accordingly

WiFiUDP UDPTestServer;
unsigned int UDPPort = 2807;
unsigned int ReturnPort = 2809;
String MiniServer = "192.168.2.102"; // the remote IP address

// IP address is fixed in router against MAC address

const int packetSize = 128;
byte packetBuffer[packetSize];

#define RELAY  12   // is connected to RELAY relay
#define SENSOR 4  // open or not sensor is GPIO4 sensor 
#define LED 13    // show a led, note, LED is inverted

#define OPEN    1
#define CLOSING 2
#define CLOSED  3
#define OPENING 4

char* replyBuffer []={"", "open", "closing", "closed", "opening"};

// Variables will change:
int doorState = CLOSED;     // the current state of the door...
int sensorState;            // the current reading from the input pin
int lastsensorState = HIGH;  // the previous reading from the input pin
int Count = 0;              // number of transitions
bool stateChange = false;         // boolean to indicate

// the following variables are unsigned long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the input pin was toggled
unsigned long debounceDelay = 100;    // the debounce time;
unsigned long waitTime = 0;          // last time pin changed state
unsigned long flashDelay = 1000;          // time for a flashing light in ms (less than 1 secs)
unsigned long movementDelay = 10000;          // time for complete open or close in ms


void setup() {

  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  pinMode(RELAY, OUTPUT);
  pinMode(SENSOR, INPUT_PULLUP);

  digitalWrite(RELAY, LOW);             // turn the relay off by making the voltage LOW

#ifdef DEBUG
  Serial.begin(115200);
  delay(10);

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
#endif

  WiFi.begin(ssid, password);

#ifdef GATE
  // deze waarde is ook gereserveerd voor 68:C6:3A:95:97:05, ons MAC adres
  WiFi.config(IPAddress(192, 168, 2, 105), IPAddress(192, 168, 2, 1), IPAddress(255, 255, 255, 0));
#endif
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
#ifdef DEBUG    
    Serial.print(".");
#endif
  }

#ifdef DEBUG    
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
#endif

  delay(1000);  // wait for ports to settle
  UDPTestServer.begin(UDPPort);
  waitTime = millis();        // set-up the first wait time
  
}

int value = 0;
int duration = 0;

void loop() {
   handleUDPServer();
   delay(10);

   // read the state of the switch into a local variable:
   int reading = digitalRead(SENSOR);

   digitalWrite(LED, reading); //  HIGH = switch off green led

   // check to see if the SENSOR has been in a stable state for long enough
   // and set the port accordingly

   // if the value has not changed for long enough... then the gate is in a stable state also (open or closed)
   if ((millis() - waitTime) > movementDelay) {
       // check if we already are at this state, or if we reached a new state...
       // determine which it is
       if (reading == HIGH && doorState != CLOSED) {
            // means gate is closed
            doorState = CLOSED;
            stateChange = true;
       }
       if (reading == LOW && doorState != OPEN) {
            // means gate is open
            doorState = OPEN;
            stateChange = true;
       }
       // reset waitTime and Count
       waitTime = millis();
       Count = 0;
   }

  // If the switch changed, due to noise or pressing:
  if (reading != lastsensorState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  // Then if we detect a state change
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the SENSOR state has changed:
    if (reading != sensorState) {
        sensorState = reading;

        // check if the sensor has changed within the flash delay
        if ((millis() - waitTime) > flashDelay) {
           // we're flashing the light which means we are moving
           Count++;
           waitTime = millis();
           // if the state is already opening or closing, just leave it there
           // otherwise, set it to the appropriate setting
           if (doorState == CLOSED) {
              doorState = OPENING;
              stateChange = true;
           }
           if (doorState == OPEN) {
              doorState = CLOSING;
              stateChange = true;
           }
        }

        // or else, was it longer than a how long since last change?
        if ((millis() - waitTime) > movementDelay) {
           // we've reached a new state... determine which it is
           if (sensorState == HIGH) {
            // means gate is closed
            doorState = CLOSED;
           } else {
            // means gate is open
            doorState = OPEN;
           }
           stateChange = true;
           // reset waitTime and Count
           waitTime = millis();
           Count = 0;           
         }
    } // end of if sensor has changed
    
  } // end of debounce

  if (stateChange) {
    stateChange = false;
    // send a reply, to the IP address and port that sent us the packet we received
    UDPTestServer.beginPacket(UDPTestServer.remoteIP(), ReturnPort);
    UDPTestServer.write(replyBuffer[doorState]);
    UDPTestServer.endPacket();
#ifdef DEBUG      
      Serial.print("Sending doorState ");
      Serial.println(replyBuffer[doorState]);
#endif   
  } // end of if statechange

  // save the reading.  Next time through the loop,
  // it'll be the lastsensorState:
  lastsensorState = reading;

}

void handleUDPServer() {
  int cb = UDPTestServer.parsePacket();
  int len;
  if (cb) {
    len = UDPTestServer.read(packetBuffer, packetSize);
    String myData = ""; 
    for(int i = 0; i < len; i++) {
      myData += (char)packetBuffer[i];
    }
    
#ifdef DEBUG    
    Serial.println(myData);
#endif
    // mydata is: open or close
    if (myData.startsWith("open")) {
      if (doorState != OPEN) {
        digitalWrite(RELAY, HIGH);            // turn the relay on, and the LED on (HIGH is the voltage level)
        delay(500);                       // wait for a bit
        digitalWrite(RELAY, LOW);             // turn the relay off by making the voltage LOW
      }
    } 
    if (myData.startsWith("close")) {
      if (doorState != CLOSED) {
        digitalWrite(RELAY, HIGH);            // turn the relay on, and the LED on (HIGH is the voltage level)
        delay(500);                       // wait for a bit
        digitalWrite(RELAY, LOW);             // turn the relay off by making the voltage LOW
      }
    }


  }
}


