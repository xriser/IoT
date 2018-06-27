#include <Arduino.h>
#include "HLW8012.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <BlynkSimpleEsp8266.h>

char auth[] = "auth_key";
BlynkTimer timer;
#define ON_OFF V0
#define PIN_TEMP V5
#define POWER V6

//D0  IO  GPIO16
//D1  IO, SCL GPIO5
//D2  IO, SDA GPIO4
//D3  IO, 10k Pull-up x
//D4  IO, 10k Pull-up, BUILTIN_LED  GPIO2
//D5  IO, SCK GPIO14
//D6  IO, MISO  GPIO12
//D7  IO, MOSI  GPIO13
//D8  IO, 10k Pull-down, SS GPIO15

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS D5

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

const char* ssid = "ssid";
const char* password = "pass";

const char *mqtt_server = "10.0.0.7";
const int mqtt_port = 1883;
const char *mqtt_user = "user";
const char *mqtt_pass = "pass";

#define BUFFER_SIZE 100
#define SERIAL_BAUDRATE                 115200
#define CALIBRATE_RUN 0
boolean run_cal =  CALIBRATE_RUN;

// GPIOs

#define RELAY1                          D1 //D1 5
//#define RELAY_PIN                     D1 //D1 5
#define SEL_PIN                         D2 //D2 4
#define CF1_PIN                         D6 //D3 0
#define CF_PIN                          D7 //D7 13

// Check values every 2 seconds
#define UPDATE_TIME                     2000

// Set SEL_PIN to HIGH to sample current
// This is the case for Itead's Sonoff POW, where a
// the SEL_PIN drives a transistor that pulls down
// the SEL pin in the HLW8012 when closed
#define CURRENT_MODE                    HIGH

// These are the nominal values for the resistors in the circuit
#define CURRENT_RESISTOR                0.001
#define VOLTAGE_RESISTOR_UPSTREAM       ( 5 * 470000 ) // Real: 2280k
#define VOLTAGE_RESISTOR_DOWNSTREAM     ( 1000 ) // Real 1.009k

#define crit_temp 61

//default
//[HLW] current multiplier : 14484.49
//[HLW] voltage multiplier : 408636.50
//[HLW] power multiplier : 10343612.00


//Calibrate: 224 60
//[HLW] current multiplier : 8687.45
//[HLW] voltage multiplier : 7191493.00
//[HLW] power multiplier : 6364848.50

//234/63
//[HLW] current multiplier : 8615.38
//[HLW] voltage multiplier : 7649133.00
//[HLW] power multiplier : 6799318.50


HLW8012 hlw8012;

// When using interrupts we have to call the library entry point
// whenever an interrupt is triggered
void ICACHE_RAM_ATTR hlw8012_cf1_interrupt() {
    hlw8012.cf1_interrupt();
}
void ICACHE_RAM_ATTR hlw8012_cf_interrupt() {
    hlw8012.cf_interrupt();
}

// Library expects an interrupt on both edges
void setInterrupts() {
    attachInterrupt(CF1_PIN, hlw8012_cf1_interrupt, CHANGE);
    attachInterrupt(CF_PIN, hlw8012_cf_interrupt, CHANGE);
}



//---------- VARIABLES ----------
float CurrentMultiplier, VoltageMultiplier, PowerMultiplier, ActivePower, Voltage, Current, ApparentPower, PowerFactor, Energy, tempC, prev_temp, delta_temp, prev_energy;
int relay_state, recon = 0, pinValue;
String log1;
char msg[50];
bool fstart;

float mq_energy;
WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  if (!fstart) {
     payload[length] = '\0';
     String s = String((char*)payload);
     mq_energy = s.toFloat();
     fstart = true;
    }
  Serial.println();

}

void reconnect() {
  // Loop until we're reconnected
  recon++;
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266boiler-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(),mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      // Once connected, publish an announcement...

      String recon_str = String(recon);
      recon_str.toCharArray(msg, recon_str.length() + 1); //packaging up the data to publish to mqtt whoa...
      client.publish("boiler/reconnect", msg, true);

      //client.publish("boiler/reconnect", "hello world");
      // ... and resubscribe
      client.subscribe("boiler/fullenergy");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(2000);
    }
  }
}



void unblockingDelay(unsigned long mseconds) {
    unsigned long timeout = millis();
    while ((millis() - timeout) < mseconds) delay(1);
}

void calibrate() {

   // Let some time to register values
    unsigned long timeout = millis();
    while ((millis() - timeout) < 10000) {
        delay(1);
    }

    // Calibrate using a 60W bulb (pure resistive) on a 230V line
    hlw8012.expectedActivePower(65.0);
    hlw8012.expectedVoltage(236.0);
    hlw8012.expectedCurrent(65.0 / 236.0);

  // Show corrected factors
//  client.print("[HLW] New current multiplier : "); client.println(hlw8012.getCurrentMultiplier());
//  client.print("[HLW] New voltage multiplier : "); client.println(hlw8012.getVoltageMultiplier());
//  client.print("[HLW] New power multiplier   : "); client.println(hlw8012.getPowerMultiplier());
//  client.println("Calibration done");
//  client.println("Reflash with determined values in the code ");

  CurrentMultiplier = hlw8012.getCurrentMultiplier();
  VoltageMultiplier = hlw8012.getVoltageMultiplier();
  PowerMultiplier = hlw8012.getPowerMultiplier();

}

//----------------------- SETUP ---------------------



void setup() {

  pinMode(RELAY1, OUTPUT);

  // Init serial port and clean garbage
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();

  sensors.begin();

 // set the resolution to 9 bit (Each Dallas/Maxim device is capable of several different resolutions)
 // sensors.setResolution(insideThermometer, 9);


  Serial.println("Booting");

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.println("Ready");

  ArduinoOTA.setHostname("boiler");
  ArduinoOTA.onStart([]() {
  Serial.println("Start");  //

  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");  //
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");  //  "Готово"
  Serial.print("IP address: ");  //  "IP-адрес: "
  Serial.println(WiFi.localIP());


    // Initialize HLW8012
    // void begin(unsigned char cf_pin, unsigned char cf1_pin, unsigned char sel_pin, unsigned char currentWhen = HIGH, bool use_interrupts = false, unsigned long pulse_timeout = PULSE_TIMEOUT);
    // * cf_pin, cf1_pin and sel_pin are GPIOs to the HLW8012 IC
    // * currentWhen is the value in sel_pin to select current sampling
    // * set use_interrupts to true to use interrupts to monitor pulse widths
    // * leave pulse_timeout to the default value, recommended when using interrupts
    hlw8012.begin(CF_PIN, CF1_PIN, SEL_PIN, CURRENT_MODE, true);

    // These values are used to calculate current, voltage and power factors as per datasheet formula
    // These are the nominal values for the Sonoff POW resistors:
    // * The CURRENT_RESISTOR is the 1milliOhm copper-manganese resistor in series with the main line
    // * The VOLTAGE_RESISTOR_UPSTREAM are the 5 470kOhm resistors in the voltage divider that feeds the V2P pin in the HLW8012
    // * The VOLTAGE_RESISTOR_DOWNSTREAM is the 1kOhm resistor in the voltage divider that feeds the V2P pin in the HLW8012

    hlw8012.setResistors(CURRENT_RESISTOR, VOLTAGE_RESISTOR_UPSTREAM, VOLTAGE_RESISTOR_DOWNSTREAM);

    // Show default (as per datasheet) multipliers
    // Serial.print("[HLW] Default current multiplier : "); Serial.println(hlw8012.getCurrentMultiplier());
    // Serial.print("[HLW] Default voltage multiplier : "); Serial.println(hlw8012.getVoltageMultiplier());
    // Serial.print("[HLW] Default power multiplier   : "); Serial.println(hlw8012.getPowerMultiplier());
    // Serial.println();

    setInterrupts();

   //calibrate();

  //CurrentMultiplier = hlw8012.getCurrentMultiplier();
  //VoltageMultiplier = hlw8012.getVoltageMultiplier();
  //PowerMultiplier = hlw8012.getPowerMultiplier();
//#define defaultCurrentMultiplier        13670.9
//#define defaultVoltageMultiplier        441250.69
//#define defaultPowerMultiplier          12168954.98


 hlw8012.setCurrentMultiplier(8755.00);
 hlw8012.setVoltageMultiplier(313000.00);
 hlw8012.setPowerMultiplier(6553038.00);


 CurrentMultiplier = hlw8012.getCurrentMultiplier();
 VoltageMultiplier = hlw8012.getVoltageMultiplier();
 PowerMultiplier = hlw8012.getPowerMultiplier();

  timer.setInterval(15000L, CheckConnection);
  Blynk.config(auth, "10.0.0.7");
  Blynk.connect();

  // LOW - power ON
  // digitalWrite(RELAY1, LOW);
  // relay_state = digitalRead(RELAY1);

  // Blynk.virtualWrite(ON_OFF, relay_state);
  Blynk.setProperty(ON_OFF, "onLabel", "ON");
  Blynk.setProperty(ON_OFF, "offLabel", "OFF");
  Blynk.setProperty(ON_OFF, "color", "#D3435C");

}
//----------------------- END SETUP ---------------------

char buffer[50];


void CheckConnection(){    // check every 15s if connected to Blynk server
  if(!Blynk.connected()){
    Serial.println("Not connected to Blynk server");
    Blynk.connect();  // try to connect to server with default timeout
  }
  else{
    Serial.println("Connected to Blynk server");
  }
}


void loop() {

if (WiFi.status() != WL_CONNECTED) {
 setup_wifi();
}
   ArduinoOTA.handle();

 if (!client.connected()) {
    reconnect();
  }
  client.loop();

 if (Blynk.connected()){
    Blynk.run();
  }

    static unsigned long last = millis();
    static unsigned long last1 = millis();
    static unsigned long last2 = millis();
    static unsigned long minute_timer = millis();


 if ((millis() - minute_timer) > 15000) {

  minute_timer = millis();
  relay_state = digitalRead(RELAY1);

  if (tempC > crit_temp) {
     digitalWrite(RELAY1, HIGH);
    }

//restore power after crit temp only if blynk button on (0)
  if ((tempC < crit_temp - 5) && (relay_state == 1) && (pinValue == 0 )) {
     digitalWrite(RELAY1, LOW);
  }

   float all_energy = mq_energy + Energy;
   //int len = strlen(String(all_energy)) + 1;
   //int len = String(all_energy).length();

 if (mq_energy > 0 && Energy > 0 && Energy - prev_energy > 1) {

//    char one_c[20];
//    dtostrf(all_energy, 10, 2, one_c);
//    snprintf (msg, sizeof(msg), "%s", one_c);
//    client.publish("boiler/energy", msg, true);


    String all_energy_str = String(all_energy); //converting all_energy (the float variable above) to a string
    all_energy_str.toCharArray(msg, all_energy_str.length() + 1); //packaging up the data to publish to mqtt whoa...
    client.publish("boiler/fullenergy", msg, true);

    String cur_energy_str = String(Energy); //converting all_energy (the float variable above) to a string
    cur_energy_str.toCharArray(msg, cur_energy_str.length() + 1); //packaging up the data to publish to mqtt whoa...
    client.publish("boiler/curenergy", msg, true);

    prev_energy = Energy;

  }

    //client.publish("boiler/debug", String(mq_energy + Energy), 1);
   // client.publish("boiler/temp", String(tempC), true);


if (tempC - prev_temp > 0) {
    delta_temp = tempC - prev_temp;
   } else {
    delta_temp = prev_temp - tempC;
    }

if (tempC > 0 && delta_temp > 0.25 ){
    //char one_c[20];
    //dtostrf(tempC, 6, 2, one_c);
    //snprintf (msg, sizeof(msg), "%s", one_c);

    String cur_temp_str = String(tempC); //converting all_energy (the float variable above) to a string
    cur_temp_str.toCharArray(msg, cur_temp_str.length() + 1); //packaging up the data to publish to mqtt whoa...
    client.publish("boiler/temp", msg, true);

    prev_temp = tempC;
  }


float power, prev_power;

//if (ActivePower > 1){

    String cur_power_str = String(ActivePower); //converting all_energy (the float variable above) to a string
    cur_power_str.toCharArray(msg, cur_power_str.length() + 1); //packaging up the data to publish to mqtt whoa...
    client.publish("boiler/curpower", msg, true);

//  }

 Blynk.virtualWrite(PIN_TEMP, tempC);
 Blynk.virtualWrite(POWER, ActivePower);

 String cur_relay_str = String(relay_state); //converting all_energy (the float variable above) to a string
 cur_relay_str.toCharArray(msg, cur_relay_str.length() + 1); //packaging up the data to publish to mqtt whoa...
 client.publish("boiler/relaystate", msg, true);

}



 if ((millis() - last1) > 5000) {

   last1 = millis();
   sensors.requestTemperatures();
   tempC = sensors.getTempCByIndex(0);
   Serial.println(tempC);

}


if ((millis() - last2) > 7000) {

   last2 = millis();
   //digitalWrite(RELAY1, LOW);
   //Serial.println("Low");
 }



    // This UPDATE_TIME should be at least twice the minimum time for the current or voltage
    // signals to stabilize. Experimentally that's about 1 second.
    if ((millis() - last) > UPDATE_TIME) {


    // Show default (as per datasheet) multipliers
//    client.print("[HLW] Default current multiplier : "); client.println(hlw8012.getCurrentMultiplier());
//    client.print("[HLW] Default voltage multiplier : "); client.println(hlw8012.getVoltageMultiplier());
//    client.print("[HLW] Default power multiplier   : "); client.println(hlw8012.getPowerMultiplier());
 //   client.println("going to cal");


   //     digitalWrite(15, HIGH);

        last = millis();
        ActivePower =  hlw8012.getActivePower();
        Voltage = hlw8012.getVoltage();
        Current = hlw8012.getCurrent();
        ApparentPower = hlw8012.getApparentPower();
        PowerFactor = (100 * hlw8012.getPowerFactor());
        Energy = hlw8012.getEnergy()/3600.00;


    }


//
// WiFiClient client = server.available();
//  if (!client) {
//    delay(1);
//    return;
//  }
//
//  //WiFiClient client = server.available();
//  char buf[20];
//  String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n ";
//  s += "<p>[HLW] current multiplier : "; s += CurrentMultiplier;
//  s += "<p>[HLW] voltage multiplier : "; s += VoltageMultiplier;
//  s += "<p>[HLW] power multiplier   : "; s += PowerMultiplier;
//
//  s += "<p>[HLW] Active Power (W)    : "; s += ActivePower;
//  s += "<p>[HLW] Voltage (V)         : "; s += Voltage;
//  s += "<p>[HLW] Current (A)         : "; s += Current;
//  s += "<p>[HLW] Apparent Power (VA) : "; s += ApparentPower;
//  s += "<p>[HLW] Power Factor (%)    : "; s += PowerFactor;
//
////dtostrf(floatvar, StringLengthIncDecimalPoint, numVarsAfterDecimal, charbuf);
//
//  s += "<p>[HLW] Energy              : "; s += dtostrf(Energy + mq_energy, 7, 3, buf);
//  s += "<p>Temp                      : "; s += tempC;
//  s += "<p>Relay                     : "; s += relay_state;
//  s += "<p>Uptime                    : "; s += millis() / 1000;
//  s += "<p>log                       : "; s += log1;
//  s += " </html>";
//
//  client.print(s);


delay(100);
timer.run();

}

// runing once connected
 BLYNK_CONNECTED() {
  Blynk.syncVirtual(ON_OFF);
 }


// running when touch the button in app
 BLYNK_WRITE(ON_OFF) {
  pinValue = param.asInt(); // assigning incoming value from pin V1 to a variable
  digitalWrite(RELAY1, pinValue);
 }
