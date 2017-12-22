#include <ESPHelper.h>
#include <ArduinoJson.h>
#include <Tasker.h>
#include <RemoteDebug.h>

#include <Nextion.h>


#include "config.h"

#define OCTO_STATE_STANDBY 1
#define OCTO_STATE_OFFLINE 2
#define OCTO_STATE_PRINTING 3
#define OCTO_STATE_UNKNOWN 4

#define MAX_FILENAME_LEN 256


// State variables, reflecting the current state determined via Octoprint API / MQTT
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
RemoteDebug Debug;

NexVariable nx_temp_bed_c = NexVariable(2,14,"temp.temp_bed_c");
NexVariable nx_temp_ext_c = NexVariable(2,14,"temp.temp_ext_c");

NexPage page2    = NexPage(2, 0, "temp");

NexTouch *nex_listen_list[] = 
{
    NULL
};

Tasker octoTasker(false);

// Open connection to the HTTP server
bool connect(const char* hostName) {
    Debug.print("Connect to ");
    Debug.println(hostName);
    
    client.setTimeout(750);
    bool ok = client.connect(hostName, 80);
    
    //Debug.println(ok ? "Connected" : "Connection Failed!");
    return ok;
}

// Send the HTTP GET request to the server
bool sendRequest(const char* host, const char* resource) {
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

// Skip HTTP headers so that we are at the beginning of the response's body
bool skipResponseHeaders() {
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

void updateT0Temperatures(float act, float tar) {
    if (abs(act-temp_t0_act)>0.5) {
        Debug.print("T0 actual temp changed. Old: ");
        Debug.print(temp_t0_act);
        Debug.print(" New: ");
        Debug.println(act);
        temp_t0_act = act;
    }
    if (tar != temp_t0_tar) {
        Debug.print("T0 target temp changed. Old: ");
        Debug.print(temp_t0_tar);
        Debug.print(" New: ");
        Debug.println(tar);
        temp_t0_tar = tar;
    }
    return;
}

void updateBedTemperatures(float act, float tar) {
    if (abs(act-temp_bed_act)>0.5) {
        Debug.print("Bed actual temp changed. Old: ");
        Debug.print(temp_bed_act);
        Debug.print(" New: ");
        Debug.println(act);
        temp_bed_act = act;
        
    }
    if (tar != temp_bed_tar) {
        Debug.print("Bed target temp changed. Old: ");
        Debug.print(temp_bed_tar);
        Debug.print(" New: ");
        Debug.println(tar);
        temp_bed_tar = tar;
    }
    nx_temp_bed_c.setValue((uint32)temp_bed_act);
    nx_temp_ext_c.setValue((uint32)temp_t0_act);
    return;
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

bool readConnectionContent() {
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

bool readPrinterContent() {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    
    JsonObject& root = jsonBuffer.parseObject(client);
    
    if (!root.success()) {
        Debug.println("JSON parsing failed!");
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
        Debug.println("JSON parsing failed!");
        return false;
    }

    
    
    
    updateJobDetails((const char*)(root["job"]["file"]["name"] | "N/A"),(float)(root["progress"]["completion"]|0),(int)(root["progress"]["printTime"]|0),(int)(root["progress"]["printTimeLeft"]|0));
    
    
    return true;
}

void disconnect() {
    //Debug.println("Disconnect");
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
    
    //Serial.begin(115200);	//start the serial line
    //delay(500);

    Debug.begin("oxtion");
    //Debug.setSerialEnabled(true);

    Debug.setResetCmdEnabled(true); // Enable the reset command
    
    Debug.println("Starting Up, Please Wait...");
    
    myESP.OTA_enable();
    //myESP.OTA_setPassword("ota.pass");
    myESP.OTA_setHostname("oxtion");
    
    myESP.begin();

    nexInit();
    
    octoTasker.setInterval(getOctoConnectionState, 5000);
    octoTasker.setInterval(getOctoPrinterState, 5000); 
    octoTasker.setInterval(getOctoJobState, 5000); 

    page2.show();
    
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
