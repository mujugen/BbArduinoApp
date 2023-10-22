#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>


#define Finger_Rx 14 // D5 yellow wire
#define Finger_Tx 12 // D6 white wire

SoftwareSerial mySerial(Finger_Rx, Finger_Tx);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

uint8_t f_buf[512];

const int maxIds = 10;
uint8_t fingerTemplate[512];
uint8_t allTemplates[maxIds][512];
bool enrolledIds[maxIds] = {false}; // Array to keep track of enrolled IDs

const char* ssid = "LHWifi";
const char* password = "Lamadrid_Wifi987";

HTTPClient http;

//WiFiServer server(80);
ESP8266WebServer webServer(80);

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
  Serial.println("Press 'e' to enroll a fingerprint, \nPress 'd' to delete a fingerprint,\nPress 'v' to verify a fingerprint...");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi Mode: ");
  Serial.println(WiFi.getMode());

  webServer.on("/api/arduinoAPITest", handleAPITest);
  webServer.begin();
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

    if (cmd == 'e') {
      if (!enrollmentMessageDisplayed) {
        Serial.println("Ready to enroll a fingerprint!");
        enrollmentMessageDisplayed = true;
      }

      // Increment ID until an available ID is found
      uint8_t id = findAvailableID();
      if (id != 0) {
        enrollFingerprint(id);
      } else {
        Serial.println("All available IDs are already used.");
      }
    } else if (cmd == 'd') {
      Serial.println("Enter the ID to delete:");
      uint8_t idToDelete = readnumber();
      if (idToDelete > 0 && idToDelete <= maxIds) {
        deleteFingerprint(idToDelete);
      } else {
        Serial.println("Invalid ID. Please enter a valid ID to delete.");
      }      
    } else if (cmd == 'v') {
      Serial.println("Place your finger on the sensor for verification...");
      verifyFingerprint();
    }
     else if (cmd == 'p') {
      Serial.println("Sending to api endpoint");
      sendMessageToAPI();
    }   
    else if (cmd == '3') {
      Serial.println("deleting all templates");
      deleteAllTemplates();
    }  
    else if (cmd == '2') {
      Serial.println("storing all templates");
      storeAllTemplates();
    }  
    else if (cmd == '1') {
      Serial.println("downloading all templates");
      downloadAllTemplates();
    }
    else if (cmd == '5') {
      Serial.println("store_template_to_buf");
      store_template_to_buf();
    }
    else if (cmd == '6') {
      Serial.println("show_from_saved");
      for (uint8_t id = 1; id <= 10; id++) {        
      show_from_saved(id);
      }
    }
    else if (cmd == '7') {
      Serial.println("write_template_data_to_sensor");
      write_template_data_to_sensor();
    }
  } 

  webServer.handleClient();
  
}

uint8_t findAvailableID() {
  // Start with ID 1 and search for an unused ID
  for (uint8_t id = 1; id <= maxIds; id++) {
    if (!enrolledIds[id]) {
      return id; // Found an available ID
    }
  }
  return 0; // No available IDs found
}

void enrollFingerprint(uint8_t currentId) {
  Serial.print("Enrolling ID #");
  Serial.println(currentId);

  uint8_t p = -1;
  Serial.print("Waiting for a valid finger to enroll as #");
  Serial.println(currentId);

  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.println(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        break;
      default:
        Serial.println("Unknown error");
        break;
    }
    delay(1000);
  }

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return;
    default:
      Serial.println("Unknown error");
      return;
  }

  Serial.println("Remove finger");
  delay(1000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.println(currentId);
  p = -1;
  Serial.println("Place the same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!
  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return;
    default:
      Serial.println("Unknown error");
      return;
  }

  // OK converted!
  Serial.print("Creating model for #");  Serial.println(currentId);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
    enrolledIds[currentId] = true; // Mark the ID as enrolled
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
  } else {
    Serial.println("Unknown error");
  }

  Serial.print("ID "); Serial.println(currentId);
  p = finger.storeModel(currentId);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
  } else {
    Serial.println("Unknown error");
  }
}

void deleteFingerprint(uint8_t idToDelete) {
  Serial.print("Deleting ID #");
  Serial.println(idToDelete);
  
  finger.deleteModel(idToDelete);
  enrolledIds[idToDelete] = false; // Mark the ID as deleted
  Serial.println("Deleted!");
}

bool isFingerprintEnrolled(uint8_t id) {
  // Check if the provided fingerprint ID is enrolled in the database
  return enrolledIds[id];
}

void verifyFingerprint() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    Serial.println("No finger detected");
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Fingerprint image conversion failed");
    return;
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint matched!");
    Serial.print("Fingerprint ID: ");
    Serial.println(finger.fingerID);
    Serial.print("Confidence: ");
    Serial.println(finger.confidence);

    // Check if the matched fingerprint ID exists in the database
    if (isFingerprintEnrolled(finger.fingerID)) {
      Serial.println("Fingerprint ID found in the database. Access granted.");
    } else {
      Serial.println("Fingerprint ID not found in the database. Access denied.");
    }
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint not found in the database. Access denied.");
  } else {
    Serial.println("Fingerprint match failed");
  }
}



void sendMessageToAPI() {
  HTTPClient http;
  WiFiClient client;

  // Create JSON object
  StaticJsonDocument<512> doc;
  JsonArray array = doc.createNestedArray("enrolledIds");
  
  for (int i = 1; i <= maxIds; i++) {
    if (enrolledIds[i]) {
      array.add(i);  // add IDs that are enrolled
    }
  }

  String json;
  serializeJson(doc, json);

  http.begin(client, "http://192.168.254.114:3000/api/test"); // Replace with your API endpoint
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(json);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("Error on sending POST request: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}


void handleAPITest() {
  Serial.println("received request");
  webServer.send(204);  // Send an empty response with 204 No Content status
}



void deleteAllTemplates() {
  for (uint8_t id = 1; id <= maxIds; id++) {
    deleteFingerprint(id);
  }
  Serial.println("All templates deleted");
}

void downloadAllTemplates(){
  for (int finger = 1; finger < 10; finger++) {
    downloadFingerprintTemplate(finger);
  }
}

void storeAllTemplates() {  // Assuming each template is 512 bytes
  for (uint8_t id = 1; id <= maxIds; id++) {
    downloadFingerprintTemplate(id);
    // Assuming downloadFingerprintTemplate stores the template in a global variable named fingerTemplate
    memcpy(allTemplates[id - 1], fingerTemplate, 512);
  }
  // Now allTemplates contains all the fingerprint templates from the sensor
  // You can send them to a server, or store them in non-volatile memory for later retrieval
}

uint8_t downloadFingerprintTemplate(uint16_t id)
{
  Serial.println("------------------------------------");
  Serial.print("Attempting to load #"); Serial.println(id);
  uint8_t p = finger.loadModel(id);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.print("Template "); Serial.print(id); Serial.println(" loaded");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    default:
      Serial.print("Unknown error "); Serial.println(p);
      return p;
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
      return p;
  }

  // one data packet is 267 bytes. in one data packet, 11 bytes are 'usesless' :D
  uint8_t bytesReceived[534]; // 2 data packets
  memset(bytesReceived, 0xff, 534);

  uint32_t starttime = millis();
  int i = 0;
  while (i < 534 && (millis() - starttime) < 20000) {
    if (mySerial.available()) {
      bytesReceived[i++] = mySerial.read();
    }
  }
  Serial.print(i); Serial.println(" bytes read.");
  Serial.println("Decoding packet...");

  uint8_t fingerTemplate[512]; // the real template
  memset(fingerTemplate, 0xff, 512);

  // filtering only the data packets
  int uindx = 9, index = 0;
  memcpy(fingerTemplate + index, bytesReceived + uindx, 256);   // first 256 bytes
  uindx += 256;       // skip data
  uindx += 2;         // skip checksum
  uindx += 9;         // skip next header
  index += 256;       // advance pointer
  memcpy(fingerTemplate + index, bytesReceived + uindx, 256);   // second 256 bytes

  for (int i = 0; i < 512; ++i) {
    //Serial.print("0x");
    printHex(fingerTemplate[i], 2);
    //Serial.print(", ");
  }
  Serial.println("\ndone.");

  return p;
}



void printHex(int num, int precision) {
  char tmp[16];
  char format[128];

  sprintf(format, "%%.%dX", precision);

  sprintf(tmp, format, num);
  Serial.print(tmp);
}


void store_template_to_buf(){

  Serial.println("Waiting for valid finger....");
  while (finger.getImage() != FINGERPRINT_OK) { // press down a finger take 1st image 
  }
  Serial.println("Image taken");


  if (finger.image2Tz(1) == FINGERPRINT_OK) { //creating the charecter file for 1st image 
    Serial.println("Image converted");
  } else {
    Serial.println("Conversion error");
    return;
  }

  Serial.println("Remove finger");
  delay(2000);
  uint8_t p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

  Serial.println("Place same finger again, waiting....");
  while (finger.getImage() != FINGERPRINT_OK) { // press the same finger again to take 2nd image
  }
  Serial.println("Image taken");


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
  
  if (finger.get_template_buffer(512, f_buf) == FINGERPRINT_OK) { //read the template data from sensor and save it to buffer f_buf
    Serial.println("Template data (comma sperated HEX):");
    for (int k = 0; k < (512/finger.packet_len); k++) { //printing out the template data in seperate rows, where row-length = packet_length
      for (int l = 0; l < finger.packet_len; l++) {
        Serial.print("0x");
        Serial.print(f_buf[(k * finger.packet_len) + l], HEX);
        Serial.print(",");
      }
      Serial.println("");
    }
  }

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
    for (int k = 0; k < 4; k++) {
      for (int l = 0; l < 128; l++) {
        Serial.print(f_buf[(k * 128) + l], HEX);
        Serial.print(",");
      }
      Serial.println("");
    }
  }
}

void write_template_data_to_sensor() {
  int template_buf_size=512; //usually hobby grade sensors have 512 byte template data, watch datasheet to know the info
  
  /*
  you can manually save the data got from "get_template.ino" example like this

  uint8_t fingerTemplate[512]={0x03,0x0E,....your template data.....};
  
  */
  memset(fingerTemplate, 0xff, 512); //comment this line if you've manually put data to the line above
  
  Serial.println("Ready to write template to sensor...");
  Serial.println("Enter the id to enroll against, i.e id (1 to 127)");
  int id = 1;
  if (id == 0) {// ID #0 not allowed, try again!
    return;
  }
  Serial.print("Writing template against ID #"); Serial.println(id);

  if (finger.write_template_to_sensor(template_buf_size,f_buf)) { //telling the sensor to download the template data to it's char buffer from upper computer (this microcontroller's "fingerTemplate" buffer)
    Serial.println("now writing to sensor...");
  } else {
    Serial.println("writing to sensor failed");
    return;
  }

  Serial.print("ID "); Serial.println(id);
  if (finger.storeModel(id) == FINGERPRINT_OK) { //saving the template against the ID you entered or manually set
    Serial.print("Successfully stored against ID#");Serial.println(id);
  } else {
    Serial.println("Storing error");
    return ;
  }
}