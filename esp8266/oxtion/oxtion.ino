#include <ESPHelper.h>
#include <ArduinoJson.h>
#include <Tasker.h>
#include <RemoteDebug.h>
#include <ESP8266HTTPClient.h>

#include <Nextion.h>


#include "config.h"

#define OCTO_STATE_STANDBY 1
#define OCTO_STATE_OFFLINE 2
#define OCTO_STATE_PRINTING 3
#define OCTO_STATE_UNKNOWN 4

#define MAX_FILENAME_LEN 256


// State variables, reflecting the current state determined via Octoprint API / MQTT
int c_state = 0;
float temp_ext_act = -1;
float temp_ext_tar = -1;
float temp_bed_act = -1;
float temp_bed_tar = -1;
char job_file[MAX_FILENAME_LEN] = "";
float job_completion = -1;
int job_time = -1;
int job_time_left = -1;
int brightness = 50;

ESPHelper myESP(&homeNet);
WiFiClient client;
RemoteDebug Debug;

// Temperature Page
NexVariable nx_temp_bed_ti = NexVariable(2,1,"temp.temp_bed_ti");
NexVariable nx_temp_bed_to = NexVariable(2,2,"temp.temp_bed_to");
NexVariable nx_temp_bed_c = NexVariable(2,14,"temp.temp_bed_c");
NexVariable nx_temp_ext_c = NexVariable(2,23,"temp.temp_ext_c");
NexVariable nx_temp_ext_ti = NexVariable(2,21,"temp.temp_ext_ti");
NexVariable nx_temp_ext_to = NexVariable(2,22,"temp.temp_ext_to");
NexHotspot nx_temp_update = NexHotspot(2,24,"update");

// Move page
NexButton nx_move_home = NexButton(3,9,"b5");
NexButton nx_move_home_x = NexButton(3,10,"b6");
NexButton nx_move_home_y = NexButton(3,11,"b7");
NexButton nx_move_home_z = NexButton(3,12,"b8");
NexButton nx_move_x_u = NexButton(3,16,"b12");
NexButton nx_move_x_d = NexButton(3,14,"b10");
NexButton nx_move_y_u = NexButton(3,13,"b9");
NexButton nx_move_y_d = NexButton(3,15,"b11");
NexButton nx_move_z_u = NexButton(3,17,"b13");
NexButton nx_move_z_d = NexButton(3,18,"b14");

NexDSButton nx_move_0_1 = NexDSButton(3,6,"bt0");
NexDSButton nx_move_1 = NexDSButton(3,7,"bt1");
NexDSButton nx_move_10 = NexDSButton(3,8,"bt2");

// LED page

NexButton nx_led_br_minus = NexButton(5,6,"b5");
NexButton nx_led_br_plus = NexButton(5,7,"b6");
NexProgressBar nx_led_brightness = NexProgressBar(5,15,"j0");
NexButton nx_led_mode_0 = NexButton(5,8,"b7");
NexButton nx_led_mode_1 = NexButton(5,9,"b8");
NexButton nx_led_mode_2 = NexButton(5,10,"b9");
NexButton nx_led_mode_3 = NexButton(5,11,"b10");
NexButton nx_led_mode_4 = NexButton(5,12,"b11");
NexButton nx_led_mode_5 = NexButton(5,13,"b12");
NexButton nx_led_mode_6 = NexButton(5,14,"b13");



NexTouch *nex_listen_list[] = 
{
    &nx_temp_update,
    &nx_move_home,
    &nx_move_home_x,
    &nx_move_home_y,
    &nx_move_home_z,
    &nx_move_x_u,
    &nx_move_x_d,
    &nx_move_y_u,
    &nx_move_y_d,
    &nx_move_z_u,
    &nx_move_z_d,
    &nx_led_br_minus,
    &nx_led_br_plus,
    &nx_led_mode_0,
    &nx_led_mode_1,
    &nx_led_mode_2,
    &nx_led_mode_3,
    &nx_led_mode_4,
    &nx_led_mode_5,
    &nx_led_mode_6,
    NULL
};

Tasker octoTasker(false);

// Open connection to the HTTP server
bool connectAPI(const char* hostName) {
    Debug.print("Connect to ");
    Debug.println(hostName);
    
    client.setTimeout(750);
    bool ok = client.connect(hostName, 80);
    
    //Debug.println(ok ? "Connected" : "Connection Failed!");
    return ok;
}

// Send the HTTP GET request to the server
bool sendAPIRequest(const char* host, const char* resource) {
    Debug.print("GET ");
    Debug.println(resource);
    
    client.print("GET ");
    client.print(resource);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.print("X-Api-Key: ");
    client.println(api_key);
    client.println("Connection: close");
    client.println();
    
    return true;
}

// Send the HTTP GET request to the server
bool postAPICommand(const char* host, const char* resource, const char* content) {
    char outBuf[128];
  
    Debug.print("POST ");
    Debug.println(resource);

    // send the header
    sprintf(outBuf,"POST %s HTTP/1.1",resource);
    client.println(outBuf);
    sprintf(outBuf,"Host: %s",host);
    client.println(outBuf);
    sprintf(outBuf,"X-Api-Key: %s",api_key);
    client.println(outBuf);
    
    client.println(F("Connection: close\r\nContent-Type: application/json"));
    sprintf(outBuf,"Content-Length: %u\r\n",strlen(content));
    client.println(outBuf);

    // send the body (variables)
    client.print(content);
    
    return true;
}

// Skip HTTP headers so that we are at the beginning of the response's body
bool skipAPIResponseHeaders() {
    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
      Debug.print(F("Unexpected response: "));
      Debug.println(status);
      return false;
    }
    // HTTP headers end with an empty line
    char endOfHeaders[] = "\r\n\r\n";
    
    client.setTimeout(10000);
    bool ok = client.find(endOfHeaders);
    
    if (!ok) {
        Debug.println("No response or invalid response!");
    }
    
    return ok;
}

void updateConnectionState(int state) {
    if (c_state != state) {
        Debug.print("Connection state changed. Old: ");
        Debug.print(c_state);
        Debug.print(" New: ");
        Debug.println(state);
        c_state = state;
    }
}

void updateExtTemperatures(float act, float tar) {
    if ((uint32)act != (uint32)temp_ext_act) {
      // change value on display
      nx_temp_ext_c.setValue((uint32)act);
    }
    temp_ext_act = act;

    if ((uint32)tar != (uint32)temp_ext_tar) {
      // change value on display
      nx_temp_ext_ti.setValue((uint32)tar);
    }
    temp_ext_tar = tar;
}

void updateBedTemperatures(float act, float tar) {
    if ((uint32)act != (uint32)temp_bed_act) {
      // change value on display
        Debug.println("updateBedTemp act");
      nx_temp_bed_c.setValue((uint32)act);
    }
    temp_bed_act = act;

    if ((uint32)tar != (uint32)temp_bed_tar) {
      // change value on display
        Debug.println("updateBedTemp tar");
      nx_temp_bed_ti.setValue((uint32)tar);
    }
    temp_bed_tar = tar;
}

void updateJobDetails(const char* file, float comp, int time, int time_left) {
    if (strcmp(file, job_file)) {
        Debug.print("New Job: ");
        Debug.println(file);
        strncpy(job_file, file, MAX_FILENAME_LEN);
    }
    job_completion = comp;
    job_time = time;
    job_time_left = time_left;
    Debug.print("Job comp: ");
    Debug.println(comp);
    Debug.print("Job time: ");
    Debug.println(time);
    Debug.print("Job left: ");
    Debug.println(time_left);
}

void updateBrightness(int br) {
    if (br < 0) {
        br = 0;
    }
    if (br > 100) {
        br = 100;
    }
    if (br != brightness) {
        brightness = br;
        nx_led_brightness.setValue(brightness);
    }
}

bool readAPIConnectionContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Debug.println("JSON parsing failed!");
        return false;
    }
    
    if (!strcmp(root["current"]["state"], "Operational")) {
        updateConnectionState(OCTO_STATE_STANDBY);
    } else if (!strcmp(root["current"]["state"], "Connecting")) {
        updateConnectionState(OCTO_STATE_STANDBY);
    } else if (!strcmp(root["current"]["state"], "Closed")) {
        updateConnectionState(OCTO_STATE_OFFLINE);
    } else if (!strcmp(root["current"]["state"], "Printing")) {
        updateConnectionState(OCTO_STATE_PRINTING);
    } else updateConnectionState(OCTO_STATE_UNKNOWN);
    
    return true;
}

bool readAPIPrinterContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Debug.println("JSON parsing failed!");
        return false;
    }
    
    updateExtTemperatures((float)root["temperature"]["tool0"]["actual"],(float)root["temperature"]["tool0"]["target"]);
    updateBedTemperatures((float)root["temperature"]["bed"]["actual"],(float)root["temperature"]["bed"]["target"]);
    
    return true;
}

bool readAPIJobContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Debug.println("JSON parsing failed!");
        return false;
    }

    
    
    
    updateJobDetails((const char*)(root["job"]["file"]["name"] | "N/A"),(float)(root["progress"]["completion"]|0),(int)(root["progress"]["printTime"]|0),(int)(root["progress"]["printTimeLeft"]|0));
    
    
    return true;
}

void disconnectAPI() {
    //Debug.println("Disconnect");
    client.stop();
}

void getAPIConnectionState(int) {
    if (connectAPI(server)) {
        if (sendAPIRequest(server, "/api/connection") && skipAPIResponseHeaders()) {
            readAPIConnectionContent();
        }
        disconnectAPI();
    }
}

void getAPIPrinterState(int) {
    if (c_state != OCTO_STATE_OFFLINE) {
        if (connectAPI(server)) {
            if (sendAPIRequest(server, "/api/printer?history=false&exclude=state,sd") && skipAPIResponseHeaders()) {
                readAPIPrinterContent();
            }
            disconnectAPI();
        }
    }
}

void getAPIJobState(int) {
    if (c_state != OCTO_STATE_OFFLINE) {
        if (connectAPI(server)) {
            if (sendAPIRequest(server, "/api/job") && skipAPIResponseHeaders()) {
                readAPIJobContent();
            }
            disconnectAPI();
        }
    }
}

void nxTemperatureCallback(void *ptr)
{
  Debug.println("Variable callback executed!!");
  // some target temp changed (extruder or bed), retreive both values
  uint32 bed_t;
  uint32 ext_t;

  nx_temp_bed_to.getValue(&bed_t);
  nx_temp_ext_to.getValue(&ext_t);

  Debug.print("New Bed Target: ");
  Debug.println(bed_t);

  Debug.print("New Extruder Target: ");
  Debug.println(ext_t);

  if (ext_t != (uint32)temp_ext_tar) {
      //updateExtTemperatures(ext_t,ext_t);
    if (connectAPI(server)) {
      char jsonCmd[256];
      snprintf(jsonCmd, 256, "{\"command\": \"target\", \"targets\": { \"tool0\": %d } }", ext_t);
      postAPICommand(server, "/api/printer/tool", jsonCmd);
      disconnectAPI();
    }
  }
  if (bed_t != (uint32)temp_bed_tar) {
     // updateBedTemperatures(bed_t,bed_t);
    if (connectAPI(server)) {
      char jsonCmd[256];
      snprintf(jsonCmd, 256, "{\"command\": \"target\", \"target\": %d }", bed_t);
      postAPICommand(server, "/api/printer/bed", jsonCmd);
      disconnectAPI();
    }
  }
  
}

void nxHomeCallback(void *ptr) {
    Debug.println("nxHomeCallback called.");
    if (ptr == &nx_move_home) {
        Debug.println("Home button pressed.");
        if (connectAPI(server)) {
            postAPICommand(server, "/api/printer/printhead", "{\"axes\":[\"x\",\"y\",\"z\"],\"command\":\"home\"}");
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_home_x) {
        Debug.println("Home X button pressed.");
        if (connectAPI(server)) {
            postAPICommand(server, "/api/printer/printhead", "{\"axes\":[\"x\"],\"command\":\"home\"}");
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_home_y) {
        Debug.println("Home Y button pressed.");
        if (connectAPI(server)) {
            postAPICommand(server, "/api/printer/printhead", "{\"axes\":[\"y\"],\"command\":\"home\"}");
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_home_z) {
        Debug.println("Home Z button pressed.");
        if (connectAPI(server)) {
            postAPICommand(server, "/api/printer/printhead", "{\"axes\":[\"z\"],\"command\":\"home\"}");
            disconnectAPI();
        }
    }
}

void nxMoveCallback(void *ptr) {
    uint32 val;
    char *step = NULL;
    
    Debug.println("nxMoveCallback called.");
    
    nx_move_0_1.getValue(&val);
    if (val == 1) {
        step = "0.1";
    } else {
        nx_move_1.getValue(&val);
        if (val == 1) {
            step = "1.0";
        } else {
            nx_move_10.getValue(&val);
            if (val == 1) {
                step = "10.0";
            } else {
                step = "0";
            }
        }
    }
    if (ptr == &nx_move_x_u) {
        //{"absolute":false,"y":-10,"command":"jog"}
        Debug.println("X Up button pressed.");
        if (connectAPI(server)) {
            char jsonCmd[256];
            snprintf(jsonCmd, 256, "{\"absolute\":false,\"x\":%s,\"command\":\"jog\"}", step);
            postAPICommand(server, "/api/printer/printhead", jsonCmd);
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_x_d) {
        //{"absolute":false,"y":-10,"command":"jog"}
        Debug.println("X Down button pressed.");
        if (connectAPI(server)) {
            char jsonCmd[256];
            snprintf(jsonCmd, 256, "{\"absolute\":false,\"x\":-%s,\"command\":\"jog\"}", step);
            postAPICommand(server, "/api/printer/printhead", jsonCmd);
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_y_u) {
        //{"absolute":false,"y":-10,"command":"jog"}
        Debug.println("Y Up button pressed.");
        if (connectAPI(server)) {
            char jsonCmd[256];
            snprintf(jsonCmd, 256, "{\"absolute\":false,\"y\":%s,\"command\":\"jog\"}", step);
            postAPICommand(server, "/api/printer/printhead", jsonCmd);
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_y_d) {
        //{"absolute":false,"y":-10,"command":"jog"}
        Debug.println("Y Down button pressed.");
        if (connectAPI(server)) {
            char jsonCmd[256];
            snprintf(jsonCmd, 256, "{\"absolute\":false,\"y\":-%s,\"command\":\"jog\"}", step);
            postAPICommand(server, "/api/printer/printhead", jsonCmd);
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_z_u) {
        //{"absolute":false,"y":-10,"command":"jog"}
        Debug.println("Z Up button pressed.");
        if (connectAPI(server)) {
            char jsonCmd[256];
            snprintf(jsonCmd, 256, "{\"absolute\":false,\"z\":%s,\"command\":\"jog\"}", step);
            postAPICommand(server, "/api/printer/printhead", jsonCmd);
            disconnectAPI();
        }
    }
    if (ptr == &nx_move_z_d) {
        //{"absolute":false,"y":-10,"command":"jog"}
        Debug.println("Z Down button pressed.");
        if (connectAPI(server)) {
            char jsonCmd[256];
            snprintf(jsonCmd, 256, "{\"absolute\":false,\"z\":-%s,\"command\":\"jog\"}", step);
            postAPICommand(server, "/api/printer/printhead", jsonCmd);
            disconnectAPI();
        }
    }
}

void nxLedCallback(void *ptr) {
    if (ptr == &nx_led_br_minus) {
        updateBrightness(brightness-10);
    }
    if (ptr == &nx_led_br_plus) {
        updateBrightness(brightness+10);
    }
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    if ((!strcmp(topic, "octoprint/temperature/bed")) && (length < 60)) {
        char buf[64];
        memcpy(buf, payload, length);
        buf[length] = '\0';

        Debug.println(buf);
    
        JsonObject& root = jsonBuffer.parseObject(buf);
        if (!root.success()) {
            Debug.println("JSON parsing failed!");
            return;
        }
        Debug.println((float)(root["actual"]));
        Debug.println((float)(root["target"]));
        updateBedTemperatures((float)(root["actual"]),(float)(root["target"]));
    }
    if ((!strcmp(topic, "octoprint/temperature/tool0")) && (length < 60)) {
        char buf[64];
        memcpy(buf, payload, length);
        buf[length] = '\0';

        Debug.println(buf);
    
        JsonObject& root = jsonBuffer.parseObject(buf);
        if (!root.success()) {
            Debug.println("JSON parsing failed!");
            return;
        }
        Debug.println((float)(root["actual"]));
        Debug.println((float)(root["target"]));
        updateExtTemperatures((float)(root["actual"]),(float)(root["target"]));
    }
    if (!strcmp(topic, "oxtion/nexUpdate")) {
        startNextionOTA(nx_ota_url);
    }
}



void setup() {
    
    //Serial.begin(115200);	//start the serial line
    //delay(500);

    Debug.begin("oxtion");
    //Debug.setSerialEnabled(true);

    Debug.setResetCmdEnabled(true); // Enable the reset command
    
    Debug.println("Starting Up, Please Wait...");
    
    myESP.OTA_enable();
    //myESP.OTA_setPassword("ota.pass");
    myESP.OTA_setHostname("oxtion");
    
    myESP.addSubscription("octoprint/temperature/bed");
    myESP.addSubscription("octoprint/temperature/tool0");
    myESP.addSubscription("oxtion/nexUpdate");
    
    myESP.setMQTTCallback(mqttCallback);

    
    myESP.begin();

    nexInit();

    nx_temp_update.attachPop(nxTemperatureCallback);
    nx_move_home.attachPop(nxHomeCallback,&nx_move_home);
    nx_move_home_x.attachPop(nxHomeCallback,&nx_move_home_x);
    nx_move_home_y.attachPop(nxHomeCallback,&nx_move_home_y);
    nx_move_home_z.attachPop(nxHomeCallback,&nx_move_home_z);
    
    nx_move_x_d.attachPop(nxMoveCallback,&nx_move_x_d);
    nx_move_x_u.attachPop(nxMoveCallback,&nx_move_x_u);
    nx_move_y_d.attachPop(nxMoveCallback,&nx_move_y_d);
    nx_move_y_u.attachPop(nxMoveCallback,&nx_move_y_u);
    nx_move_z_d.attachPop(nxMoveCallback,&nx_move_z_d);
    nx_move_z_u.attachPop(nxMoveCallback,&nx_move_z_u);
    
    nx_led_br_minus.attachPop(nxLedCallback,&nx_led_br_minus);
    nx_led_br_plus.attachPop(nxLedCallback,&nx_led_br_plus);
    nx_led_mode_0.attachPop(nxLedCallback,&nx_led_mode_0);
    nx_led_mode_1.attachPop(nxLedCallback,&nx_led_mode_1);
    nx_led_mode_2.attachPop(nxLedCallback,&nx_led_mode_2);
    nx_led_mode_3.attachPop(nxLedCallback,&nx_led_mode_3);
    nx_led_mode_4.attachPop(nxLedCallback,&nx_led_mode_4);
    nx_led_mode_5.attachPop(nxLedCallback,&nx_led_mode_5);
    nx_led_mode_6.attachPop(nxLedCallback,&nx_led_mode_6);
    
   // octoTasker.setInterval(getAPIConnectionState, 5000);
   // octoTasker.setInterval(getAPIPrinterState, 5000); 
   // octoTasker.setInterval(getAPIJobState, 5000); 
    
    Debug.println("Initialization Finished.");
}

void loop(){
    myESP.loop();  //run the loop() method as often as possible - this keeps the network services running
    octoTasker.loop();
    Debug.handle();
    nexLoop(nex_listen_list);

    
    delay(25);
    
    yield();
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Upload firmware to the Nextion LCD
void startNextionOTA (String otaURL) {
  // based in large part on code posted by indev2 here:
  // http://support.iteadstudio.com/support/discussions/topics/11000007686/page/2
  byte nextionSuffix[] = {0xFF, 0xFF, 0xFF};
  int nextionResponseTimeout = 500; // timeout for receiving ack string in milliseconds
  unsigned long nextionResponseTimer = millis(); // record current time for our timeout

  int FileSize = 0;
  String nexcmd;
  int count = 0;
  byte partNum = 0;
  int total = 0;
  int pCent = 0;

  Debug.println("LCD OTA: Attempting firmware download from:" + otaURL);
  HTTPClient http;
  http.begin(otaURL);
  int httpCode = http.GET();
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Debug.println("LCD OTA: HTTP GET return code:" + String(httpCode));
    if (httpCode == HTTP_CODE_OK) { // file found at server
      // get length of document (is -1 when Server sends no Content-Length header)
      int len = http.getSize();
      FileSize = len;
      int Parts = (len / 4096) + 1;
      Debug.println("LCD OTA: File found at Server. Size " + String(len) + " bytes in " + String(Parts) + " 4k chunks.");
      // create buffer for read
      uint8_t buff[128] = {};
      // get tcp stream
      WiFiClient * stream = http.getStreamPtr();

      Debug.println("LCD OTA: Issuing NULL command");
      Serial.write(nextionSuffix, sizeof(nextionSuffix));
      //handleNextionInput();
      delay(250);
      while (Serial.available()) {
        byte inByte = Serial.read();
      }

      String nexcmd = "whmi-wri " + String(FileSize) + ",115200,0";
      Debug.println("LCD OTA: Sending LCD upload command: " + nexcmd);
      Serial.print(nexcmd);
      Serial.write(nextionSuffix, sizeof(nextionSuffix));
      Serial.flush();

      if (otaReturnSuccess()) {
        Debug.println("LCD OTA: LCD upload command accepted");
      }
      else {
        Debug.println("LCD OTA: LCD upload command FAILED.");
        return;
      }
      Debug.println("LCD OTA: Starting update");
      while (http.connected() && (len > 0 || len == -1)) {
        // get available data size
        size_t size = stream->available();
        if (size) {
          // read up to 128 bytes
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          // write it to Serial
          Serial.write(buff, c);
          Serial.flush();
          count += c;
          if (count == 4096) {
            nextionResponseTimer = millis();
            partNum ++;
            total += count;
            pCent = (total * 100) / FileSize;
            count = 0;
            if (otaReturnSuccess()) {
              Debug.println("LCD OTA: Part " + String(partNum) + " OK, " + String(pCent) + "% complete");
            }
            else {
              Debug.println("LCD OTA: Part " + String(partNum) + " FAILED, " + String(pCent) + "% complete");
            }
          }
          if (len > 0) {
            len -= c;
          }
        }
        //delay(1);
      }
      partNum++;
      //delay (250);
      total += count;
      if ((total == FileSize) && otaReturnSuccess()) {
        Debug.println("LCD OTA: success, wrote " + String(total) + " of " + String(FileSize) + " bytes.");
      }
      else {
        Debug.println("LCD OTA: failure");
      }
    }
  }
  else {
    Debug.println("LCD OTA: HTTP GET failed, error code " + http.errorToString(httpCode));
  }
  http.end();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Monitor the serial port for a 0x05 response within our timeout
bool otaReturnSuccess() {
  int nextionCommandTimeout = 1000; // timeout for receiving termination string in milliseconds
  unsigned long nextionCommandTimer = millis(); // record current time for our timeout
  bool otaSuccessVal = false;
  while ((millis() - nextionCommandTimer) < nextionCommandTimeout) {
    if (Serial.available()) {
      byte inByte = Serial.read();
      Debug.println(inByte);
      if (inByte == 0x5) {
        otaSuccessVal = true;
        break;
      }
    }
    else {
      delay (1);
    }
  }
  delay (10);
  return otaSuccessVal;
}
