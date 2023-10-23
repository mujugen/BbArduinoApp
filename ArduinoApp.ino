#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>



#define Finger_Rx 14 // D5 yellow wire
#define Finger_Tx 12 // D6 white wire

SoftwareSerial mySerial(Finger_Rx, Finger_Tx);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

uint8_t f_buf[512];

const int maxIds = 10;
uint8_t fingerTemplate[512];
uint8_t fingerTemplate2[512];
uint8_t fingerTemplate3[512];
uint8_t allTemplates[maxIds][512];
bool enrolledIds[maxIds] = {false};
String bufferString = "";

const char* ssid = "LHWifi";
const char* password = "Lamadrid_Wifi987";

const char* webAppIP = "192.168.254.110";

HTTPClient http;

//WiFiServer server(80);
ESP8266WebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);


bool enrollmentMessageDisplayed = false;

void setup() {
  Serial.begin(9600);
  while (!Serial); // Wait for Serial Monitor to open
  delay(100);
  Serial.println("\n\nAdafruit Fingerprint sensor enrollment");


  // Set the data rate for the sensor serial port
  finger.begin(9600);

  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }

  
  Serial.println(F("Reading sensor parameters"));
  finger.getParameters();
  Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);

  finger.getTemplateCount();

  if (finger.templateCount == 0) {
    Serial.print("Sensor doesn't contain any fingerprint data. Please run the 'enroll' example.");
  } else {
    Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
  }

  WiFi.disconnect(true);
  delay(100);

  Serial.print("WiFi Mode before setting: ");
  Serial.println(WiFi.getMode());

  WiFi.mode(WIFI_STA);

  Serial.print("WiFi Mode right after setting: ");
  Serial.println(WiFi.getMode());

  delay(500);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(3000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println("Waiting for command");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi Mode: ");
  Serial.println(WiFi.getMode());

  webServer.on("/api/enrollAPI", enrollAPI);
  webServer.on("/api/verifyAPI", verifyAPI);
  webServer.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

}

uint8_t readnumber(void) {
  uint8_t num = 0;

  while (num == 0) {
    while (! Serial.available());
    num = Serial.parseInt();
  }
  return num;
}

unsigned long lastMessageSent = 0;
const unsigned long messageInterval = 500;

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == '1') {
      Serial.println("store_template_to_buf");
      store_template_to_buf();
    }
    else if (cmd == '2') {
      Serial.println("show_from_saved");
      for (uint8_t id = 1; id <= 4; id++) {        
      show_from_saved(id);
      }
    }
    else if (cmd == '3') {     
      verifyFingerprint();
      
    }
  } 

  webServer.handleClient();
  webSocket.loop();
  
}


bool verifyFingerprint() {
  uint8_t p = 0;
  const int maxRetries = 3;
  webSocket.broadcastTXT("verifyFingerprint");
  for (int attempt = 1; attempt <= maxRetries; ++attempt) {    
    webSocket.broadcastTXT("Place finger on scanner");
    Serial.println("Place finger on scanner");
    while (finger.getImage() != FINGERPRINT_OK) {
      p = finger.getImage();
    }
    Serial.println("Remove finger");
    webSocket.broadcastTXT("Remove finger");

    p = finger.image2Tz();
    if (p != FINGERPRINT_OK) {
      Serial.println("Fingerprint image conversion failed");
      webSocket.broadcastTXT("Scan failed");

      // If not the last attempt, delay and retry
      if (attempt != maxRetries) {
        delay(1000);
        
        continue;
      } else {
        return false;
      }
    }

    p = finger.fingerSearch();
    if (p == FINGERPRINT_OK) {
      Serial.println("Fingerprint matched!");
      Serial.print("Fingerprint ID: ");
      Serial.println(finger.fingerID);
      Serial.print("Confidence: ");
      Serial.println(finger.confidence);

      webSocket.broadcastTXT("Scan success");
      Serial.println("Scan success");
      return true;
    } else if (p == FINGERPRINT_NOTFOUND) {
      Serial.println("Scan failed");
      webSocket.broadcastTXT("Scan failed");
      
      // If not the last attempt, delay and retry
      if (attempt != maxRetries) {
        delay(1000);
        Serial.println("Retrying");
        continue;
      } else {
        Serial.println("Scan failed");
        return false;
      }
    } else {
      Serial.println("Scan failed");
      webSocket.broadcastTXT("Scan failed");
      
      // If not the last attempt, delay and retry
      if (attempt != maxRetries) {
        delay(1000);  // adjust the delay as needed
        continue;
      } else {
        Serial.println("Scan failed");
        return false;
      }
    }
  }
  Serial.println("Scan failed");
  return false;
}


void store_template_to_buf(){
  uint32_t starttime = millis();
  Serial.println("Waiting for valid finger....");
  webSocket.broadcastTXT("Place finger");
  while (finger.getImage() != FINGERPRINT_OK) {
  }
  Serial.println("Image taken");
  webSocket.broadcastTXT("Remove finger");


  if (finger.image2Tz(1) == FINGERPRINT_OK) { //creating the charecter file for 1st image 
    Serial.println("Image converted");
  } else {
    Serial.println("Conversion error");
    return;
  }

  Serial.println("Remove finger");
  // send status to web server then client to remove finger
  delay(2000);
  uint8_t p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  webSocket.broadcastTXT("Place finger");
  Serial.println("Place same finger again, waiting....");
  while (finger.getImage() != FINGERPRINT_OK) {
  }
  Serial.println("Image taken");
  // send status to web server then client that it's processing
  webSocket.broadcastTXT("Processing");
  if (finger.image2Tz(2) == FINGERPRINT_OK) { //creating the charecter file for 2nd image 
    Serial.println("Image converted");
  } else {
    Serial.println("Conversion error");
    return;
  }


  Serial.println("Creating model...");

  if (finger.createModel() == FINGERPRINT_OK) {  //creating the template from the 2 charecter files and saving it to char buffer 1
    Serial.println("Prints matched!");
    Serial.println("Template created");
  } else {
    Serial.println("Template not build");
    return;
  }

  Serial.println("Attempting to get template..."); 
  if (finger.getModel() == FINGERPRINT_OK) {  //requesting sensor to transfer the template data to upper computer (this microcontroller)
    Serial.println("Transferring Template...."); 
  } else {
    Serial.println("Failed to transfer template");
    return;
  }

  bufferString = "";
  if (finger.get_template_buffer(512, f_buf) == FINGERPRINT_OK) {
    memcpy(fingerTemplate2, f_buf, 512);
    Serial.println("fingerTemplate2:");
    for (int i = 0; i < 512; i++) {
      //Serial.print("0x");
      bufferString += String(fingerTemplate2[i], HEX);
      bufferString += ",";
    }
    Serial.println(bufferString);    
    Serial.println();
  }
  
  webSocket.broadcastTXT("Success");

}

void show_from_saved(uint16_t id) {
  Serial.println("------------------------------------");
  Serial.print("Attempting to load #"); Serial.println(id);
  uint8_t p = finger.loadModel(id);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.print("Template "); Serial.print(id); Serial.println(" loaded");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return;
    default:
      Serial.print("Unknown error "); Serial.println(p);
      return ;
  }

  // OK success!

  Serial.print("Attempting to get #"); Serial.println(id);
  p = finger.getModel();
  switch (p) {
    case FINGERPRINT_OK:
      Serial.print("Template "); Serial.print(id); Serial.println(" transferring:");
      break;
    default:
      Serial.print("Unknown error "); Serial.println(p);
      return;
  }
  
  uint8_t f_buf[512];
  if (finger.get_template_buffer(512, f_buf) == FINGERPRINT_OK) {
    bufferString = "0";
    for (int k = 0; k < 4; k++) {
      for (int l = 0; l < 128; l++) {
        //Serial.print(f_buf[(k * 128) + l], HEX);
        //Serial.print(",");
        bufferString += "0x";
        bufferString += String(f_buf[(k * finger.packet_len) + l], HEX);
        bufferString += ",";
      }
      //Serial.println("");
    }
     Serial.println(bufferString);
  }
  
}


void write_template_data_to_sensor() {
    int template_buf_size = 512;

    Serial.println("Ready to write template to sensor...");
    Serial.println("Enter the id to enroll against, i.e id (1 to 127)");
    int id = 1;
    Serial.print("Writing template against ID #"); Serial.println(id);

    if (finger.write_template_to_sensor(template_buf_size, fingerTemplate3)) {
        Serial.println("now writing to sensor...");
        webSocket.broadcastTXT("Processing");
    } else {
        Serial.println("writing to sensor failed");
        return;
    }

    Serial.print("ID "); Serial.println(id);
    if (finger.storeModel(id) == FINGERPRINT_OK) {
        Serial.print("Successfully stored against ID#");Serial.println(id);
        return;
    } else {
        Serial.println("Storing error");
        return;
    }
}




void enrollAPI() {
    int counter = 0;
    finger.emptyDatabase();
    
    webSocket.broadcastTXT("Starting");
    bufferString = "";
    
    uint8_t fingerTemplate[512];

    while (bufferString == "" && counter <= 3) {
        Serial.println("enrollAPI");
        store_template_to_buf();
        counter += 1;

        // If the loop has iterated 3 times without success, send an error message.
        if (counter > 3) {
            webSocket.broadcastTXT("Failed");
            webServer.send(500, "text/plain");
            return;
        }
    }
    
    // If the loop exited due to bufferString being non-empty, send a success message.
    if (bufferString != "") {
        webSocket.broadcastTXT("Waiting");
        webServer.send(200, "text/plain", bufferString);
    }
}

void verifyAPI() {
  Serial.println("verifyAPI");
  //finger.emptyDatabase();
  webSocket.broadcastTXT("Verify request received");

  // Assuming you are using some library that allows POST data retrieval. 
  // Modify according to the actual library you are using.
  if (webServer.hasArg("plain")) {
    String bufferValue = webServer.arg("plain");
    Serial.println(bufferValue);  

    char bufferCopy[3*512+1];
    strcpy(bufferCopy, bufferValue.c_str());
    char* token = strtok(bufferCopy, ",");
    int index = 0;

    while (token != nullptr && index < 512) {
      fingerTemplate3[index] = strtol(token, nullptr, 16); // Convert the hex string to an integer
      token = strtok(nullptr, ",");
      index++;
    }

    // Print the copied data to verify
    Serial.println("fingerTemplate3:");
    for (int i = 0; i < 512; i++) {
      Serial.print("0x");
      Serial.print(fingerTemplate3[i], HEX);
      Serial.print(",");
    }
    write_template_data_to_sensor();
    Serial.println("Writing to sensor finished");
    bool result = verifyFingerprint();    
    if (result) {
    webServer.send(200, "text/plain", "Fingerprint match");
  } else {
    webServer.send(200, "text/plain", "Fingerprint not matched");
  }
  } else {
    webServer.send(400, "text/plain", "No buffer parameter provided");
  }
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            break;
        case WStype_TEXT:
            String text = String((char *) &payload[0]);
            // Handle received text if needed
            break;
    }
}
