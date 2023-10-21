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

const int maxIds = 128; // Maximum number of fingerprint IDs
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
  finger.begin(57600);

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
    while (!Serial.available());
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
        sendToServer(id); // Send the ID to the server
      } else {
        Serial.println("All available IDs are already used.");
      }
    } else if (cmd == 'd') {
      Serial.println("Enter the ID to delete:");
      uint8_t idToDelete = readnumber();
      if (idToDelete > 0 && idToDelete <= maxIds) {
        deleteFingerprint(idToDelete);
        sendDeleteRequest(idToDelete);
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


void sendToServer(uint8_t currentId) {
  HTTPClient http;
  WiFiClient client;
  http.begin(client, "http://192.168.1.4/Enrolltest1.php");  // Replace with your server URL
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "FINGERID=" + String(currentId);

  int httpCode = http.POST(postData);
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.println(response);
    Serial.println("ID sent to server successfully.");
  } else {
    Serial.println("Error sending ID to server.");
  }

  http.end();
}

void sendDeleteRequest(uint8_t idToDelete) {
  HTTPClient http;
  WiFiClient client;
  http.begin(client, "http://192.168.1.4/DeleteTest1.php");  // Replace with your server URL
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postDeleteData = "FINGERID=" + String(idToDelete);
  
  int httpCode = http.POST(postDeleteData);

  if (httpCode == HTTP_CODE_OK) {
    String dresponse = http.getString();
    Serial.println(dresponse);
    Serial.println("Fingerprint deleted successfully.");
  } else {
    Serial.println("Error deleting fingerprint from the server.");
  }

  http.end();
}

void sendverifyFingerprint(uint8_t currentId) {
  HTTPClient http;
  WiFiClient client;
  http.begin(client, "http://192.168.1.4/Verify.php");  // Replace with your server URL
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String postData = "FINGERID=" + String(currentId);

  int httpCode = http.POST(postData);
    String vresponse = http.getString();
    Serial.println(vresponse);

    if (vresponse.indexOf("exists") >= 0) {
      Serial.println("Fingerprint ID exists in the database.");
    } else {
      Serial.println("Fingerprint ID does not exist in the database.");
    }
  

  http.end();
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
