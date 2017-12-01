#include "ESPHelper.h"
#include <ArduinoJson.h>
#include "Tasker.h"

#include "config.h"

#define OCTO_STATE_STANDBY 1
#define OCTO_STATE_OFFLINE 2
#define OCTO_STATE_PRINTING 3
#define OCTO_STATE_UNKNOWN 4

#define MAX_FILENAME_LEN 256


// State variables, reflecting the current state determined via Octoprint API
int c_state = 0;
float temp_t0_act = -1;
float temp_t0_tar = -1;
float temp_bed_act = -1;
float temp_bed_tar = -1;
char job_file[MAX_FILENAME_LEN] = "";
float job_completion = -1;
int job_time = -1;
int job_time_left = -1;

ESPHelper myESP(&homeNet);
WiFiClient client;

Tasker octoTasker(false);

// Open connection to the HTTP server
bool connect(const char* hostName) {
    //Serial.print("Connect to ");
    //Serial.println(hostName);
    
    client.setTimeout(750);
    bool ok = client.connect(hostName, 80);
    
    //Serial.println(ok ? "Connected" : "Connection Failed!");
    return ok;
}

// Send the HTTP GET request to the server
bool sendRequest(const char* host, const char* resource) {
    //Serial.print("GET ");
    //Serial.println(resource);
    
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

// Skip HTTP headers so that we are at the beginning of the response's body
bool skipResponseHeaders() {
    // HTTP headers end with an empty line
    char endOfHeaders[] = "\r\n\r\n";
    
    client.setTimeout(10000);
    bool ok = client.find(endOfHeaders);
    
    if (!ok) {
        Serial.println("No response or invalid response!");
    }
    
    return ok;
}

void updateConnectionState(int state) {
    if (c_state != state) {
        Serial.print("Connection state changed. Old: ");
        Serial.print(c_state);
        Serial.print(" New: ");
        Serial.println(state);
        c_state = state;
    }
}

void updateT0Temperatures(float act, float tar) {
    if (abs(act-temp_t0_act)>0.5) {
        Serial.print("T0 actual temp changed. Old: ");
        Serial.print(temp_t0_act);
        Serial.print(" New: ");
        Serial.println(act);
        temp_t0_act = act;
    }
    if (tar != temp_t0_tar) {
        Serial.print("T0 target temp changed. Old: ");
        Serial.print(temp_t0_tar);
        Serial.print(" New: ");
        Serial.println(tar);
        temp_t0_tar = tar;
    }
    return;
}

void updateBedTemperatures(float act, float tar) {
    if (abs(act-temp_bed_act)>0.5) {
        Serial.print("Bed actual temp changed. Old: ");
        Serial.print(temp_bed_act);
        Serial.print(" New: ");
        Serial.println(act);
        temp_bed_act = act;
    }
    if (tar != temp_bed_tar) {
        Serial.print("Bed target temp changed. Old: ");
        Serial.print(temp_bed_tar);
        Serial.print(" New: ");
        Serial.println(tar);
        temp_bed_tar = tar;
    }
    return;
}

void updateJobDetails(const char* file, float comp, int time, int time_left) {
    if (strcmp(file, job_file)) {
        Serial.print("New Job: ");
        Serial.println(file);
        strncpy(job_file, file, MAX_FILENAME_LEN);
    }
    job_completion = comp;
    job_time = time;
    job_time_left = time_left;
    Serial.print("Job comp: ");
    Serial.println(comp);
    Serial.print("Job time: ");
    Serial.println(time);
    Serial.print("Job left: ");
    Serial.println(time_left);
}

bool readConnectionContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Serial.println("JSON parsing failed!");
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

bool readPrinterContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Serial.println("JSON parsing failed!");
        return false;
    }
    
    updateT0Temperatures((float)root["temperature"]["tool0"]["actual"],(float)root["temperature"]["tool0"]["target"]);
    updateBedTemperatures((float)root["temperature"]["bed"]["actual"],(float)root["temperature"]["bed"]["target"]);
    
    return true;
}

bool readJobContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Serial.println("JSON parsing failed!");
        return false;
    }
    
    
    updateJobDetails((const char*)root["job"]["file"]["name"],(float)root["progress"]["completion"],(int)root["progress"]["printTime"],(int)root["progress"]["printTimeLeft"]);
    
    
    return true;
}

void disconnect() {
    //Serial.println("Disconnect");
    client.stop();
}

void getOctoConnectionState(int) {
    if (connect(server)) {
        if (sendRequest(server, "/api/connection") && skipResponseHeaders()) {
            readConnectionContent();
        }
        disconnect();
    }
}

void getOctoPrinterState(int) {
    if (c_state != OCTO_STATE_OFFLINE) {
        if (connect(server)) {
            if (sendRequest(server, "/api/printer?history=false&exclude=state,sd") && skipResponseHeaders()) {
                readPrinterContent();
            }
            disconnect();
        }
    }
}

void getOctoJobState(int) {
    if (c_state != OCTO_STATE_OFFLINE) {
        if (connect(server)) {
            if (sendRequest(server, "/api/job") && skipResponseHeaders()) {
                readJobContent();
            }
            disconnect();
        }
    }
}

void setup() {
    
    Serial.begin(115200);	//start the serial line
    delay(500);
    
    Serial.println("Starting Up, Please Wait...");
    
    myESP.OTA_enable();
    myESP.OTA_setPassword("ota.pass");
    
    myESP.begin();
    
    octoTasker.setInterval(getOctoConnectionState, 5000);
    octoTasker.setInterval(getOctoPrinterState, 5000); 
    octoTasker.setInterval(getOctoJobState, 5000); 
    
    
    Serial.println("Initialization Finished.");
}

void loop(){
    myESP.loop();  //run the loop() method as often as possible - this keeps the network services running
    octoTasker.loop();
    
    delay(25);
    
    yield();
}
