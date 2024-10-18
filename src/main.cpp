#include <Arduino.h>
#include <painlessMesh.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wpa2.h>  // For WPA2 Enterprise networks
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <FastLED.h>

// Constants for OLED and LEDs
#define OLED_CLOCK    15
#define OLED_DATA    4
#define OLED_RESET    16
#define LED_PIN        5
#define NUM_LEDS     5

// LED configuration
CRGB g_LEDs[NUM_LEDS] = {0};

// Mesh network settings
#define MESH_PREFIX "esp32_mesh"
#define MESH_PASSWORD "mesh_password"
#define MESH_PORT 5555
uint32_t recent_node = 12345678;

// Wi-Fi credentials
const char* ssid = "eduroam";
const char* identity = "londal@bc.edu"; 
const char* password = "Chris21bc";
#define SERVER_PORT 8080

test

// Web server setup
WebServer server(SERVER_PORT);

// Variables for tracking connection status
bool wifiConnected = false;

// Extensions
String links[2] = {"/", "/api/readings"};

// OLED configuration
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_OLED(U8G2_R2, OLED_RESET, OLED_CLOCK, OLED_DATA);
int g_lineHeight = 0;
int g_Brightness = 255;
int g_PowerLimit = 3000;

// Keys, data, and suffix arrays for sensor values
String keys[5] = {"PM 1.0", "PM 2.5", "PM 10.0", "Temperature", "Humidity"};    // Keys for data
String datum[5] = {"1", "2", "3", "4", "5"};    // pm1.0, pm2.5, pm10.0, temp, hum (placeholder values)
String suf[5] = {"ppm", "ppm", "ppm", "F", "%"};    // Suffixes for readings
const char * messages[5] = {" ", " ", " ", " ", " "};
const char * num[100] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", 
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", 
    "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", 
    "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", 
    "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", 
    "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", 
    "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", 
    "71", "72", "73", "74", "75", "76", "77", "78", "79", "80", 
    "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", 
    "91", "92", "93", "94", "95", "96", "97", "98", "99", "100"
    };

//String to send to other nodes with sensor readings
String readings;
JsonDocument jsonReadings;  // JSON document to hold readings

Scheduler userScheduler;  // Task scheduler for painlessMesh
painlessMesh mesh;	// Mesh network instance

// User Stub
void fillLED(CRGB color);
void displayOLED(uint32_t nodeId, String key, String status);
void updateAirQuality(uint32_t nodeId, String key, int airQuality);
void connectToWiFi();
void disconnectFromWiFi();
void sendDataToServer();
void disconnectFromMesh();
void reconnectToMesh();
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);
void serialDelay(int seconds);

std::map<uint32_t, std::map<String, int>> dataMap;

void mapAdd(uint32_t node, String key, int value) {
    dataMap[node][key] = value;
}

int mapValue(uint32_t node, String key) {
    int value = dataMap[node][key];
    return value;
}

// Node data and status
std::map<uint32_t, std::map<String, String>>  nodeStatus;
unsigned long lastMessageTime = 0;

void updateMessages(const char * msg) {
    for (int i = 4; i > 0; i--) {
        messages[i] = messages[i - 1];
    }
    messages[0] = msg;
}

void displayMessages() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("Msg: %s", messages[i]);
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

String readingsToJSON() {
	jsonReadings["Node"] = String(recent_node);
    for (int i = 0; i < 5; i++) {
        jsonReadings[keys[i]] = String(dataMap[recent_node][keys[i]]);
    }
	serializeJson(jsonReadings, readings);  // Serialize the JSON object into a string
    return readings;
}

// Handler for the root URL of the web server
void handleRoot() {
    Serial.println("Handling root request");  // Add this for debugging
    String html = "<html><head><title>Mesh Network Monitor</title>";
    html += "<meta http-equiv=\"refresh\" content=\"30\">";  // Auto-refresh the page every 30 seconds
    html += "</head><body><h1>Sensor Readings</h1><ul>";

	html += "<li> Node ID: " + String(recent_node) + "</li>";  // Display node ID

    for (int i = 0; i < 5; i++) {
        html += "<li>" + String(keys[i]) + ": " + String(dataMap[recent_node][keys[i]]) + " " + String(suf[i]) + "</li>";  // Display sensor readings
    }
    html += "<ul></body></html>";
    server.send(200, "text/html", html);  // Send HTML to the client
}

// Send data to server using JSON
void sendDataToServer() {
	Serial.println("Handling JSON request");  // Add this for debugging
    String jsonStr;
	readingsToJSON();  // Convert readings to JSON
    serializeJson(jsonReadings, jsonStr);
    server.send(200, "application/json", jsonStr);  // Send JSON to the client
}

// // Handler for the /api/readings URL, which serves JSON data
// void handleJson() {
//     Serial.println("Handling JSON request");  // Add this for debugging
//     String jsonOutput;
//     readingsToJSON();  // Convert readings to JSON
//     serializeJson(jsonReadings, jsonOutput);  // Serialize the JSON object into a string
//     server.send(200, "application/json", jsonOutput);  // Send JSON to the client
// }

// Start the web server and define the routes
void startWebServer() {
    server.on("/", handleRoot);  // Serve web page at the root URL
    // server.on("/api/readings", sendDataToServer);  // Serve JSON at /api/readings
    server.begin();  // Start the web server
    Serial.println("Web server started!");
    updateMessages("Web server started!");
    displayMessages();
}

// Function to stop the web server
void stopWebServer() {
    server.stop();  // Stop the server
    Serial.println("\nWeb server stopped.");
    updateMessages("Web server stopped.");
    displayMessages();
}

// Callback for receiving data from mesh
void receivedCallback(uint32_t from, String &msg) {
	recent_node = from;
    JsonDocument doc;
    deserializeJson(doc, msg);
	Serial.println("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
	Serial.println(msg);
	Serial.println("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
    
	for (int i = 0; i < 5; i++) {
		mapAdd(from, keys[i], doc[keys[i]].as<int>());
		// dataMap[from][keys[i]] = doc[keys[i]].as<int>();
		Serial.print(keys[i]);
        Serial.print(": ");
        Serial.print(dataMap[from][keys[i]]);
        Serial.print(" ");
        Serial.println(suf[i]);
	}

    // // Store the node data and update the timestamp
    // nodeStatus[from] = doc["status"];
    // lastMessageTime = millis();

    // // Check air quality and update LEDs and OLED
    // updateAirQuality(from, doc["value"]);
    
    // // Send data to the web server
    disconnectFromMesh();
    connectToWiFi();
	if (wifiConnected = true) {
		serialDelay(30);
	}
    disconnectFromWiFi();
    reconnectToMesh();
}

// Update air quality and control LEDs and OLED
void updateAirQuality(uint32_t nodeId, String key, int airQuality) {
    String status;
    if (airQuality <= 50) {
        status = "Good";
        fillLED(CRGB::Green);
    } else if (airQuality <= 100) {
        status = "Moderate";
        fillLED(CRGB::Yellow);
    } else if (airQuality <= 150) {
        status = "Unhealthy for Sensitive Groups";
        fillLED(CRGB::Orange);
    } else if (airQuality <= 200) {
        status = "Unhealthy";
        fillLED(CRGB::Red);
    } else if (airQuality <= 300) {
        status = "Very Unhealthy";
        fillLED(CRGB::Purple);
    } else {
        status = "Hazardous";
        fillLED(CRGB::Maroon);
    }

    nodeStatus[nodeId][key] = status;
    displayOLED(nodeId, key, status);
}

// Display information on OLED
void displayOLED(uint32_t nodeId, String key, String status) {
    g_OLED.clear();
    g_OLED.drawStr(0, g_lineHeight, ("Node: " + String(nodeId)).c_str());
    g_OLED.drawStr(0, g_lineHeight * 2, ("Key: " + key).c_str());
	g_OLED.drawStr(0, g_lineHeight * 3, ("Status: " + status).c_str());
    g_OLED.sendBuffer();
}

void generateLinks() {
    for (int i = 0; i < (sizeof(links) / sizeof(links[0])); i++) {
        Serial.printf("http://");
        Serial.print(WiFi.localIP());
        Serial.print(":" + String(SERVER_PORT));
        Serial.println(links[i]);
    }
}

// Connect to WiFi
void connectToWiFi() {
    Serial.print("Connecting to WiFi...");

    // Disconnect from any previous Wi-Fi connections
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);    // Set to Station mode

    // WPA2 Enterprise requires EAP (Extensible Authentication Protocol)
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)identity, strlen(identity));    // Identity (username)
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)identity, strlen(identity));    // Some networks need this too
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password, strlen(password));    // Password

    // WPA2 Enterprise setup
    esp_wifi_sta_wpa2_ent_enable();    // Enable WPA2 Enterprise authentication
    
    int num_try = 0;
    
    // Start the connection
    WiFi.begin(ssid);
    
    while (WiFi.status() != WL_CONNECTED && num_try < 25) {
        delay(500);
        Serial.print(".");
        num_try++;
    }

	if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected!");
        updateMessages("WiFi connected!");
        displayMessages();
        wifiConnected = true;
        startWebServer();  // Start the web server after successful connection
        generateLinks();
    } else {
        Serial.println("Failed to connect to WiFi");
        updateMessages("Failed to connect to WiFi");
        displayMessages();
        wifiConnected = false;
    }

}

// Disconnect from WiFi
void disconnectFromWiFi() {
	stopWebServer();  // Stop the server
    WiFi.disconnect();
	wifiConnected = false;
	Serial.println("WiFi Disconnected.");
    updateMessages("WiFi Disconnected.");
    displayMessages();
    
}

// Disconnect from mesh
void disconnectFromMesh() {
    mesh.stop();
}

// Reconnect to mesh
void reconnectToMesh() {
    Serial.println("\nReconnecting to mesh.");
    updateMessages("Reconnecting to mesh.");
    displayMessages();

    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
	// mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE);  // all types on
	// mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages
    
    // Set the mesh callbacks
	mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
	mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
    
    Serial.println("\nMesh Reconnected.");
    updateMessages("Mesh Reconnected.");
    displayMessages();
}

// Fill LED strip with color
void fillLED(CRGB color) {
    for (int i = 0; i < NUM_LEDS; i++) {
        g_LEDs[i] = color;
    }
    FastLED.show();
}

// Convert int to const char*
const char* intToConstChar(int value) {
    std::string str = std::to_string(value);  // Convert int to std::string
    return str.c_str();  // Convert std::string to const char*
}

void serialDelay(int seconds) {
	Serial.println("");
	for (int i = 0; i < seconds; i++) {
		Serial.println(i + 1);
        updateMessages(num[i]);
        displayMessages();
		delay(1000);
	}
}

// Callbacks for mesh network
void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("\nNew Connection: %u\n", nodeId);
}

void changedConnectionCallback() {
    Serial.printf("\nMesh Connections Changed.\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("\nAdjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

void setup() {
    Serial.begin(115200);
	Serial.println("\nStart");

    // Initialize OLED
    g_OLED.begin();
    g_OLED.clear();
    g_OLED.setFont(u8g2_font_profont15_tf);    
    g_lineHeight = g_OLED.getFontAscent() - g_OLED.getFontDescent();

    // Initialize FastLED
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(g_LEDs, NUM_LEDS);
    FastLED.setBrightness(g_Brightness);
    FastLED.setMaxPowerInMilliWatts(g_PowerLimit);

    serialDelay(5);

    // Mesh network initialization
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
	// mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE);  // all types on
    // mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages
    
	// Set the mesh callbacks
	mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
	mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    displayMessages();

}

void loop() {
    // Handle mesh network and web server
	if (wifiConnected == true) {
        server.handleClient();
    } else {
        mesh.update();
    }
}