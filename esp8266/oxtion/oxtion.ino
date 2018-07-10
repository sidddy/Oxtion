#define MQTT_MAX_PACKET_SIZE 1024

#include <ESPHelper.h>
#include <ArduinoJson.h>
#include <Tasker.h>
#include <RemoteDebug.h>
#include <ESP8266HTTPClient.h>

#include <Nextion.h>

#include "config.h"

#define OCTO_STATE_UNKNOWN 0
#define OCTO_STATE_STANDBY 1
#define OCTO_STATE_OFFLINE 2
#define OCTO_STATE_PRINTING 3
#define OCTO_STATE_LOADED 4
#define OCTO_STATE_PAUSED 5

#define OCTO_STATE_MAX 5

const char *stateString[OCTO_STATE_MAX+1] = { "Unknown", "Operational", "Offline", "Printing", "Operational", "Paused" };

#define MAX_FILENAME_LEN 256
#define MAX_FILE_COUNT 100

#define SLEEP_TIMEOUT 300*1000

#define BUTTON_CONNECT 11
#define BUTTON_CANCEL 13
#define BUTTON_DISABLED 15
#define BUTTON_PAUSE 17
#define BUTTON_PRINT 19
#define BUTTON_RESUME 21

const int stateButtons[OCTO_STATE_MAX+1][2] = { {BUTTON_DISABLED, BUTTON_DISABLED}, //unknown
                                                {BUTTON_DISABLED, BUTTON_DISABLED}, //standby
                                                {BUTTON_CONNECT, BUTTON_DISABLED},  //offline
                                                {BUTTON_PAUSE, BUTTON_CANCEL},      //printing
                                                {BUTTON_PRINT, BUTTON_DISABLED},    //loaded
                                                {BUTTON_RESUME, BUTTON_CANCEL} };    //paused
                                                
                         
typedef struct {
    char* name;
    short success;
} file_entry_t;
                         
typedef struct {
    char* directories[MAX_FILE_COUNT];
    file_entry_t files[MAX_FILE_COUNT];
    uint dir_count;
    uint file_count;
} folder_t;

// State variables, reflecting the current state determined via Octoprint API / MQTT
int c_state = -1;
float temp_ext_act = -1;
float temp_ext_tar = -1;
float temp_bed_act = -1;
float temp_bed_tar = -1;
char job_file[MAX_FILENAME_LEN] = "";
char c_folder[MAX_FILENAME_LEN] = "/";
int c_folder_pos = 0;
folder_t c_folder_content;
float job_completion = -1;
int job_time = -1;
int job_time_left = -1;
int brightness = -1;
unsigned long last_button;

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
NexProgressBar nx_led_brightness = NexProgressBar(5,15,"led.j0");
NexButton nx_led_mode_0 = NexButton(5,8,"b7");
NexButton nx_led_mode_1 = NexButton(5,9,"b8");
NexButton nx_led_mode_2 = NexButton(5,10,"b9");
NexButton nx_led_mode_3 = NexButton(5,11,"b10");
NexButton nx_led_mode_4 = NexButton(5,12,"b11");
NexButton nx_led_mode_5 = NexButton(5,13,"b12");
NexButton nx_led_mode_6 = NexButton(5,14,"b13");

// Main page
NexText nx_main_state = NexText(1,7,"main.t0");
NexText nx_main_file = NexText(1,8,"main.t1");
NexText nx_main_compl = NexText(1,9,"main.t2");
NexText nx_main_time = NexText(1,10,"main.t3");
NexText nx_main_left = NexText(1,11,"main.t4");
NexButton nx_main_button1 = NexButton(1,11,"main.b5");
NexButton nx_main_button2 = NexButton(1,12,"main.b6");
NexProgressBar nx_main_progress = NexProgressBar(1,19,"main.j0");

// Cancel page
NexButton nx_cancel_yes = NexButton(6,3,"cancel.b0");
NexButton nx_cancel_no = NexButton(6,4,"cancel.b1");

// Pause page
NexButton nx_pause_yes = NexButton(7,3,"pause.b0");
NexButton nx_pause_no = NexButton(7,4,"pause.b1");

// File page
NexButton nx_file_but_up = NexButton(4,6,"print.b5");
NexButton nx_file_but_down = NexButton(4,7,"print.b6");
NexText nx_file_loc = NexText(4,8,"print.t0");
NexButton nx_file_lines[5] = { NexButton(4,9,"print.b7"), NexButton(4,10,"print.b8"), NexButton(4,11,"print.b9"), NexButton(4,12,"print.b10"), NexButton(4,13,"print.b11") };
NexNumber nx_file_no_0 = NexNumber(4,14,"print.n0");
NexNumber nx_file_no_1 = NexNumber(4,15,"print.n1");

// screensaver page
NexPage nx_screensaver = NexPage(8,0,"screensaver");

// startup page
NexPage nx_startup = NexPage(0,0,"startup");

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
    &nx_main_button1,
    &nx_main_button2,
    &nx_cancel_yes,
    &nx_cancel_no,
    &nx_pause_yes,
    &nx_pause_no,
    &nx_file_but_up,
    &nx_file_but_down,
    &nx_file_lines[0],
    &nx_file_lines[1],
    &nx_file_lines[2],
    &nx_file_lines[3],
    &nx_file_lines[4],
    &nx_screensaver,
    &nx_startup,
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
    client.println("X-Requested-With: XMLHttpRequest");
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
    if ((c_state != state) and (state >= 0) and (state<=OCTO_STATE_MAX)) {
        /*Debug.print("Connection state changed. Old: ");
        Debug.print(c_state);
        Debug.print(" New: ");
        Debug.println(state);*/
        c_state = state;
        nx_main_state.setText(stateString[c_state]);
        char buf[20];
        snprintf(buf,20,"main.b5.pic=%d",stateButtons[c_state][0]);
        Debug.println(buf);
        sendCommand(buf);
        snprintf(buf,20,"main.b6.pic=%d",stateButtons[c_state][1]);
        Debug.println(buf);
        sendCommand(buf);
        snprintf(buf,20,"main.b5.pic2=%d",stateButtons[c_state][0]+1);
        Debug.println(buf);
        sendCommand(buf);
        snprintf(buf,20,"main.b6.pic2=%d",stateButtons[c_state][1]+1);
        Debug.println(buf);
        sendCommand(buf);
        
    }
}

void updateExtTemperatures(float act, float tar) {
    if ((int32)act != (int32)temp_ext_act) {
      // change value on display
      nx_temp_ext_c.setValue((uint32)act);
    }
    temp_ext_act = act;

    if ((int32)tar != (int32)temp_ext_tar) {
      // change value on display
      nx_temp_ext_ti.setValue((uint32)tar);
    }
    temp_ext_tar = tar;
}

void updateBedTemperatures(float act, float tar) {
    if ((int32)act != (int32)temp_bed_act) {
      // change value on display
        Debug.println("updateBedTemp act");
      nx_temp_bed_c.setValue((uint32)act);
    }
    temp_bed_act = act;

    if ((int32)tar != (int32)temp_bed_tar) {
      // change value on display
        Debug.println("updateBedTemp tar");
      nx_temp_bed_ti.setValue((uint32)tar);
    }
    temp_bed_tar = tar;
}

void updateJobDetails(const char* file, float comp, int time, int time_left) {
    if ((strlen(file)>0) && (c_state == OCTO_STATE_STANDBY)) {
        updateConnectionState(OCTO_STATE_LOADED);
    }
    if (strcmp(file, job_file)) {
        strncpy(job_file, file, MAX_FILENAME_LEN);
        nx_main_file.setText(file);
    }
    
    char buf[20];
    int h,m,s;
    
    if (comp != job_completion) {
        job_completion = comp;
        snprintf(buf,20,"%d.%02d%%",(int)(job_completion),((int)(job_completion*100))%100);
        nx_main_compl.setText(buf);
        nx_main_progress.setValue((int)(job_completion));
    }
    
    if (job_time != time) {
        job_time = time;
        h = (int)(time/3600);
        m = (int)((time-h*3600)/60);
        s = time-h*3600-m*60;
    
        snprintf(buf,20,"%02d:%02d:%02d",h,m,s);
        nx_main_time.setText(buf);
    }
    
    if (time_left < 0) {
        time_left = 0;
    }
    
    if (job_time_left != time_left) {
        job_time_left = time_left;
    
        h = (int)(time_left/3600);
        m = (int)((time_left-h*3600)/60);
        s = time_left-h*3600-m*60;
    
        snprintf(buf,20,"%02d:%02d:%02d",h,m,s);
        nx_main_left.setText(buf);
    }
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
        char buf[10];
        snprintf(buf,10,"%d",(int)((brightness*255)/100));
        myESP.publish("octoled/brightness",buf);
    }
}

void updateFolder(int pos) {
    int d_cnt = c_folder_content.dir_count;
    int f_cnt = c_folder_content.file_count;
    int lpos = 0;
    char buf[128];
    
    if ((d_cnt == 0) && (f_cnt == 0)) {
        for (int i=0; i<5; i++) {
          nx_file_lines[i].setText("");
        }
        return;
    }
    
    if (pos < 0)
        pos = 0;
    if (pos >= d_cnt + f_cnt)
        pos = d_cnt + f_cnt - 1;
    
    
    
    c_folder_pos = pos;
    
    while (lpos < 5) {
        if (pos+lpos < d_cnt) {
            snprintf(buf,128,"print.b%d.pco=BLACK", lpos+7);
            sendCommand(buf);
            snprintf(buf,128,"print.b%d.pco2=BLACK", lpos+7);
            sendCommand(buf);
            snprintf(buf,128,"<DIR> %s",c_folder_content.directories[pos+lpos]);
            nx_file_lines[lpos].setText(buf);
        } else if (pos+lpos-d_cnt < f_cnt) {
            if (c_folder_content.files[pos+lpos-d_cnt].success == 1) {
                snprintf(buf,128,"print.b%d.pco=GREEN", lpos+7); //TBD
                sendCommand(buf);
                snprintf(buf,128,"print.b%d.pco2=GREEN", lpos+7);
                sendCommand(buf);
            } else if (c_folder_content.files[pos+lpos-d_cnt].success == -1) {
                snprintf(buf,128,"print.b%d.pco=RED", lpos+7);
                sendCommand(buf);
                snprintf(buf,128,"print.b%d.pco2=RED", lpos+7);
                sendCommand(buf);
           } else {
                snprintf(buf,128,"print.b%d.pco=BLACK", lpos+7);
                sendCommand(buf);
                snprintf(buf,128,"print.b%d.pco2=BLACK", lpos+7);
                sendCommand(buf);
           }
            nx_file_lines[lpos].setText(c_folder_content.files[pos+lpos-d_cnt].name);
        } else {
            nx_file_lines[lpos].setText("");
        }
        lpos++;
    }
    
    nx_file_no_0.setValue(pos+1);
    nx_file_no_1.setValue(d_cnt+f_cnt);

    snprintf(buf,128,"Location: %s",c_folder); 
    nx_file_loc.setText(buf);
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
        if (strlen(job_file)>0) {
            updateConnectionState(OCTO_STATE_LOADED);
        } else {
            updateConnectionState(OCTO_STATE_STANDBY);
        }
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
    
    updateJobDetails((root["job"]["file"]["name"]|"n/a"),(root["progress"]["completion"]|0.0),(root["progress"]["printTime"]|0),(root["progress"]["printTimeLeft"]|0));
    
    return true;
}

bool readAPIFolderContent() {
    
    DynamicJsonBuffer jsonFolderBuffer(1024);
    JsonObject& root = jsonFolderBuffer.parseObject(client);
    
    if (!root.success()) {
        Debug.println("JSON parsing failed!");
        root.printTo(Debug);
        Debug.println("");
        return false;
    }
    
  /*  root.printTo(Debug);
    Debug.println("");
    jsonFolderRoot = &root;
    jsonFolderRoot->printTo(Debug);
    Debug.println("");*/
  
    // clean up old folder data
    c_folder_content.dir_count = 0;
    c_folder_content.file_count = 0;
    for (int i=0; i<MAX_FILE_COUNT; i++) {
        if (c_folder_content.directories[i] != NULL) {
            free(c_folder_content.directories[i]);
            c_folder_content.directories[i] = NULL;
        }
        if (c_folder_content.files[i].name != NULL) {
            free(c_folder_content.files[i].name);
            c_folder_content.files[i].name = NULL;
        }
        c_folder_content.files[i].success = 0;
    }
    
    // build new folder data
     
    c_folder_content.dir_count = (root["directories"].size()<=MAX_FILE_COUNT)?root["directories"].size():100;
    c_folder_content.file_count = (root["files"].size()<=MAX_FILE_COUNT)?root["files"].size():100;
    
    for (int i=0; i < c_folder_content.dir_count; i++) {
        const char* dir = root["directories"][i];
        c_folder_content.directories[i] = (char*) malloc(strlen(dir)+1);
        strcpy(c_folder_content.directories[i],dir);
    }
    for (int i=0; i < c_folder_content.file_count; i++) {
        const char* file = root["files"][i][0];
        const char* succ = root["files"][i][1];
        c_folder_content.files[i].name = (char*) malloc(strlen(file)+1);
        strcpy(c_folder_content.files[i].name,file);
        if (strcmp(succ,"succ") == 0) {
            c_folder_content.files[i].success = 1;
        } else if (strcmp(succ,"err") == 0) {
            c_folder_content.files[i].success = -1;
        } else c_folder_content.files[i].success = 0;
    }
    
    strncpy(c_folder,root["path"],MAX_FILENAME_LEN);
  
    updateFolder(0);
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
    if (connectAPI(server)) {
        if (sendAPIRequest(server, "/api/job") && skipAPIResponseHeaders()) {
            readAPIJobContent();
        }
        disconnectAPI();
    }
}

void getAPIFolderInfo(char* folder) {
    if (connectAPI(server)) {
        char buf[MAX_FILENAME_LEN+20];
        snprintf(buf,MAX_FILENAME_LEN+20,"/api/plugin/oxtion_plugin?path=%s",folder);
        if (sendAPIRequest(server, buf) && skipAPIResponseHeaders()) {
            readAPIFolderContent();
       }
       disconnectAPI();
    }
}

void nxTemperatureCallback(void *ptr)
{
  last_button = millis();
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
    last_button = millis();
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
    
    last_button = millis();
    
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
    last_button = millis();
    if (ptr == &nx_led_br_minus) {
        updateBrightness(brightness-10);
    } else  if (ptr == &nx_led_br_plus) {
        updateBrightness(brightness+10);
    } else if (ptr == &nx_led_mode_0) {
        myESP.publish("octoled/mode","0");
    } else if (ptr == &nx_led_mode_1) {
        myESP.publish("octoled/mode","1");
    } else if (ptr == &nx_led_mode_2) {
        myESP.publish("octoled/mode","2");
    } else if (ptr == &nx_led_mode_3) {
        myESP.publish("octoled/mode","3");
    } else if (ptr == &nx_led_mode_4) {
        myESP.publish("octoled/mode","4");
    } else if (ptr == &nx_led_mode_5) {
        myESP.publish("octoled/mode","5");
    } else if (ptr == &nx_led_mode_6) {
        myESP.publish("octoled/mode","6");
    } 
}

void nxMainCallback(void *ptr) {
    last_button = millis();
    Debug.println("main pop");
    if (ptr == &nx_main_button1) {
         Debug.println("main pop but1");
        if (stateButtons[c_state][0] == BUTTON_CONNECT) {
            Debug.println("Main pop connect");
            if (connectAPI(server)) {
                Debug.println("connected to api");
                postAPICommand(server, "/api/connection", "{\"command\": \"connect\" }");
                disconnectAPI();
            }
        } else if (stateButtons[c_state][0] == BUTTON_PAUSE) {
            sendCommand("page pause");
        } else if (stateButtons[c_state][0] == BUTTON_RESUME) {
            if (connectAPI(server)) {
                postAPICommand(server, "/api/job", "{\"command\": \"pause\", \"action\": \"resume\" }");
                disconnectAPI();                                                                                                                                            
            } 
        } else if (stateButtons[c_state][0] == BUTTON_PRINT) {
            if (connectAPI(server)) {
                postAPICommand(server, "/api/job", "{\"command\": \"start\" }");                                                                    
                disconnectAPI();                                                                                                                                            
            }                                                                                                                                                               
        }
    } else if (ptr == &nx_main_button2) {
        //Debug.println("main pop but2");
        if (stateButtons[c_state][1] == BUTTON_CANCEL) {
            sendCommand("page cancel");
        }
        /*else if (stateButtons[c_state][1] == BUTTON_DISABLED) {
            Debug.println("trying job api call");
            getAPIJobState(0);
        }*/
    }
}

void nxCancelCallback(void *ptr) {
    last_button = millis();
    if (ptr == &nx_cancel_no) {
        sendCommand("page main");
    } else if (ptr == &nx_cancel_yes) {
        if (connectAPI(server)) {
            postAPICommand(server, "/api/job", "{\"command\": \"cancel\" }");
            disconnectAPI();
        }
        sendCommand("page main");
    }
}

void nxPauseCallback(void *ptr) {
    last_button = millis();
    if (ptr == &nx_pause_no) {
        sendCommand("page main");
    } else if (ptr == &nx_pause_yes) {
        if (connectAPI(server)) {
            postAPICommand(server, "/api/job", "{\"command\": \"pause\", \"action\": \"pause\" }");
            disconnectAPI();
        }
        sendCommand("page main");
    }
}

void nxFileCallback(void *ptr) {
    last_button = millis();
    Debug.println("File pop callback");
    if (ptr == &nx_file_but_up) {
        if (c_folder_pos == 0) {
            getAPIFolderInfo(c_folder);
        } else {
            updateFolder(c_folder_pos-4);
        }
    } else if (ptr == &nx_file_but_down) {
        updateFolder(c_folder_pos+4);
    } else {
        for (int i=0; i<5; i++) {
            if (ptr == &nx_file_lines[i]) {
             //TBD  c_folder_pos 
               int pos = c_folder_pos + i;
               int d_cnt = c_folder_content.dir_count;
               int f_cnt = c_folder_content.file_count;
               if (pos < d_cnt) {
                   // a directory was selected
                   char* dir = c_folder_content.directories[pos];
                   if (strcmp(dir,"..")==0) {
                       char folder[MAX_FILENAME_LEN];
                       strcpy(folder,c_folder);
                       for (int i=strlen(folder)-2; i>=0; i--) {
                          if (folder[i] == '/') {
                              folder[i+1] = '\0';
                              getAPIFolderInfo(folder);
                              break;
                          }
                       }
                   } else {
                      char folder[MAX_FILENAME_LEN];
                      snprintf(folder,MAX_FILENAME_LEN,"%s%s/",c_folder,dir);
                      getAPIFolderInfo(folder);
                  }
               } else {
                    // file was selected
                    pos = pos - d_cnt;
                    if (pos < f_cnt) {
                       const char* fn = c_folder_content.files[pos].name;
                       char buf[3*MAX_FILENAME_LEN];
                       snprintf(buf,3*MAX_FILENAME_LEN,"/api/files/local%s%s",c_folder,fn);
                       Debug.println(buf); 
                       if (connectAPI(server)) { 
                           postAPICommand(server, buf, "{\"command\": \"select\" }");
                           disconnectAPI();
                       }
                   }
               }
               break;
            }
        }
    }
}

void nxScreensaverCallback(void *ptr) {
    last_button = millis();
    sendCommand("page main");
    sendCommand("dim=100");
}

void nxStartupCallback(void *ptr) {
    // uww... if we end up here, the Nextion device crashed and the user touched the display on the startup page. Go to main page (current display state will be lost...)
    last_button = millis();
    sendCommand("page main");
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    const size_t BUFFER_SIZE = 1024;
    
    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
    //Debug.print("MQTT Callback, topic: ");
    //Debug.println(topic);
    
    if ((!strcmp(topic, "octoprint/temperature/bed")) && (length < 60)) {
        char buf[64];
        memcpy(buf, payload, length);
        buf[length] = '\0';

        //Debug.println(buf);
    
        JsonObject& root = jsonBuffer.parseObject(buf);
        if (!root.success()) {
            Debug.println("JSON parsing failed!");
            return;
        }
        //Debug.println((float)(root["actual"]));
        //Debug.println((float)(root["target"]));
        updateBedTemperatures((float)(root["actual"]),(float)(root["target"]));
    } else if ((!strcmp(topic, "octoprint/temperature/tool0")) && (length < 128)) {
        char buf[128];
        memcpy(buf, payload, length);
        buf[length] = '\0';

        //Debug.println(buf);
    
        JsonObject& root = jsonBuffer.parseObject(buf);
        if (!root.success()) {
            Debug.println("JSON parsing failed!");
            return;
        }
        //Debug.println((float)(root["actual"]));
        //Debug.println((float)(root["target"]));
        updateExtTemperatures((float)(root["actual"]),(float)(root["target"]));
    } else if (!strcmp(topic, "oxtion/nexUpdate")) {
        startNextionOTA(nx_ota_url);
    } else if (!strcmp(topic, "octoprint/event/Connected")) {
        if (strlen(job_file)>0) {
            updateConnectionState(OCTO_STATE_LOADED);
        } else {
            updateConnectionState(OCTO_STATE_STANDBY);
        }
    } else if (!strcmp(topic, "octoprint/event/Disconnected")) {
        updateConnectionState(OCTO_STATE_OFFLINE);
    } else if (!strcmp(topic, "octoprint/event/Shutdown")) {
        updateConnectionState(OCTO_STATE_OFFLINE);
    } else if (!strcmp(topic, "octoprint/event/PrintStarted")) {
        updateConnectionState(OCTO_STATE_PRINTING);
    } else if (!strcmp(topic, "octoprint/event/PrintResumed")) {
        updateConnectionState(OCTO_STATE_PRINTING);
    } else if (!strcmp(topic, "octoprint/event/PrintPaused")) {
        updateConnectionState(OCTO_STATE_PAUSED);
    } else if (!strcmp(topic, "octoprint/event/PrintFailed")) {
        if (strlen(job_file)>0) {
            updateConnectionState(OCTO_STATE_LOADED);
        } else {
            updateConnectionState(OCTO_STATE_STANDBY);
        }
    } else if ((!strcmp(topic, "octoprint/event/FileSelected")) && (length < 1024)) {
        char buf[1024];
        memcpy(buf, payload, length);
        buf[length] = '\0';
    
        JsonObject& root = jsonBuffer.parseObject(buf);
        if (!root.success()) {
            Debug.println("JSON parsing failed!");
            return;
        }
        updateJobDetails(root["name"], job_completion, job_time, job_time_left);
        
    } else if ((!strcmp(topic, "oxtion/estimate")) && (length < 128)) {
        char buf[128];
        memcpy(buf, payload, length);
        buf[length] = '\0';
    
        JsonObject& root = jsonBuffer.parseObject(buf);
        if (!root.success()) {
            Debug.println("JSON parsing failed!");
            return;
        }
        updateJobDetails(job_file, (float)root["progress"], (int)root["printtime"], (int)root["printtimeleft"]);
    }  else if (!strcmp(topic, "oxtion/startup")) {
        // Octoprint just started up. lets get its current state.
        getAPIConnectionState(0);
        getAPIJobState(0);
        getAPIFolderInfo("/");
    }
    
}

void wifiCallback() {
    getAPIConnectionState(0);
    //getAPIPrinterState(0);
    getAPIJobState(0);
    getAPIFolderInfo("/");
}


void timerTask(int) {
    if (c_state == OCTO_STATE_PRINTING) {
        updateJobDetails(job_file, job_completion, job_time+1, job_time_left-1);
    }
    if ((last_button != 0) && ((millis() - last_button) > SLEEP_TIMEOUT)) {
        sendCommand("page screensaver");
        sendCommand("dim=1");
        last_button = 0;
    }
}


void setup() {
    
    //Serial.begin(115200);	//start the serial line
    //delay(500);
    pinMode(D1, OUTPUT);  
    digitalWrite(D1, HIGH);
    delay(500);

    Debug.begin("oxtion");
    //Debug.setSerialEnabled(true);

    Debug.setResetCmdEnabled(true); // Enable the reset command
    
    Debug.println("Starting Up, Please Wait...");
    
    myESP.OTA_enable();
    //myESP.OTA_setPassword("ota.pass");
    myESP.OTA_setHostname("oxtion");
    
    myESP.addSubscription("octoprint/temperature/bed");
    myESP.addSubscription("octoprint/temperature/tool0");
    myESP.addSubscription("oxtion/#");
    myESP.addSubscription("octoprint/event/#");
    
    //myESP.addSubscription("oxtion/nexUpdate");
    
    
    myESP.setMQTTCallback(mqttCallback);
    myESP.setWifiCallback(wifiCallback);

    
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
    
    nx_main_button1.attachPop(nxMainCallback,&nx_main_button1);
    nx_main_button2.attachPop(nxMainCallback,&nx_main_button2);

    nx_cancel_yes.attachPop(nxCancelCallback,&nx_cancel_yes);
    nx_cancel_no.attachPop(nxCancelCallback,&nx_cancel_no);
    
    nx_pause_yes.attachPop(nxPauseCallback,&nx_pause_yes);
    nx_pause_no.attachPop(nxPauseCallback,&nx_pause_no);
    
    
    nx_file_but_up.attachPop(nxFileCallback,&nx_file_but_up);
    nx_file_but_down.attachPop(nxFileCallback,&nx_file_but_down);
    for (int i=0; i<5; i++) {
        nx_file_lines[i].attachPop(nxFileCallback,&nx_file_lines[i]);
        nx_file_lines[i].setText("");
    }
    nx_file_loc.setText("");
    
    nx_screensaver.attachPop(nxScreensaverCallback, &nx_screensaver);
    nx_startup.attachPop(nxStartupCallback, &nx_startup);
    
    updateBrightness(50);
    updateBedTemperatures(0,0);
    updateExtTemperatures(0,0);
    updateConnectionState(OCTO_STATE_UNKNOWN);
    
    nx_main_state.setText("");
    nx_main_file.setText("");
    nx_main_compl.setText("");
    nx_main_time.setText("");
    nx_main_left.setText("");
    sendCommand("page main");
    sendCommand("dim=100");
    
    
    c_folder_content.dir_count = 0;
    c_folder_content.file_count = 0;
    for (int i=0; i<MAX_FILE_COUNT; i++) {
        c_folder_content.directories[i] = NULL;
        c_folder_content.files[i].name = NULL;
        c_folder_content.files[i].success = 0;
    }
    
    octoTasker.setInterval(timerTask, 1000);
    
    last_button = millis();
    
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
        yield();
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
              //Debug.println("LCD OTA: Part " + String(partNum) + " OK, " + String(pCent) + "% complete");
            }
            else {
              //Debug.println("LCD OTA: Part " + String(partNum) + " FAILED, " + String(pCent) + "% complete");
            }
          }
          if (len > 0) {
            len -= c;
          }
        }
        //delay(1);
        yield();
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
