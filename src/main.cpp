#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HardwareSerial.h>

#define BUFFER_SIZE 128

int defaultTcpPort = 5000; // Default TCP port for GPS data

Preferences prefs;
WebServer webServer(80);
WiFiServer gpsTcpServer(defaultTcpPort);  // default port
HardwareSerial gpsSerial(1);

String wifiMode, wifiSSID, wifiPASS;
int tcpPort = defaultTcpPort;
bool shouldStartAP = true;

void handleRoot() {
  String html = R"rawliteral(
    <html><body>
    <h2>ESP32 GPS Setup</h2>
    <form action="/save" method="post">
    <label><input type="radio" name="mode" value="AP" %APCHECKED%> Access Point</label><br>
    <label><input type="radio" name="mode" value="STA" %STACHECKED%> Connect to Wi-Fi</label><br><br>
    SSID: <input name="ssid" value="%SSID%"><br>
    Password: <input type="password" name="pass" value="%PASS%"><br>
    TCP Port: <input name="port" value="%PORT%"><br><br>
    <input type="submit" value="Save and Reboot">
    </form><br>
    <form action="/reset" method="post">
    <input type="submit" value="Factory Reset">
    </form>
    <hr>
    <a href="/status">View Status</a>
    </body></html>
  )rawliteral";

  html.replace("%APCHECKED%", (wifiMode == "AP" ? "checked" : ""));
  html.replace("%STACHECKED%", (wifiMode == "STA" ? "checked" : ""));
  html.replace("%SSID%", wifiSSID);
  html.replace("%PASS%", wifiPASS);
  html.replace("%PORT%", String(tcpPort));

  webServer.send(200, "text/html", html);
}

void handleSave() {
  String mode = webServer.arg("mode");
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  int port = webServer.arg("port").toInt();

  prefs.begin("config", false);
  prefs.putString("mode", mode);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putInt("port", port);
  prefs.end();

  webServer.send(200, "text/plain", "Saved. Rebooting...");
  delay(1000);
  ESP.restart();
}

void handleReset() {
  prefs.begin("config", false);
  prefs.clear();
  prefs.end();
  webServer.send(200, "text/plain", "Factory reset. Rebooting...");
  delay(1000);
  ESP.restart();
}

void handleStatus() {
  String html = "<html><body><h2>Status</h2>";
  html += "WiFi Mode: " + wifiMode + "<br>";
  html += "IP: " + WiFi.localIP().toString() + "<br>";
  html += "TCP Port: " + String(tcpPort) + "<br>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void setupWiFi() {
  prefs.begin("config", true);
  wifiMode = prefs.getString("mode", "AP");
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  tcpPort = prefs.getInt("port", defaultTcpPort);
  prefs.end();

  if (wifiMode == "STA" && wifiSSID.length() > 0) {
    WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
    Serial.println("Connecting to WiFi...");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
      shouldStartAP = false;
    } else {
      Serial.println("\nFailed. Falling back to AP mode.");
    }
  }

  if (shouldStartAP || wifiMode == "AP") {
    WiFi.softAP("ESP-GPS");
    Serial.println("AP Mode. IP: " + WiFi.softAPIP().toString());
  }
}

void startWebServer() {
  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/reset", HTTP_POST, handleReset);
  webServer.on("/status", handleStatus);
  webServer.begin();
  Serial.println("Web server started.");

  if (MDNS.begin("esp32")) {
    Serial.println("mDNS responder started: http://esp32.local");
  }
}

void startTCPServer() {
  gpsTcpServer = WiFiServer(tcpPort);
  gpsTcpServer.begin();
  Serial.println("TCP Server started on port " + String(tcpPort));
}

void setup() {
  Serial.begin(115200); // Monitor serial (USB)
  // Start GPS UART
  gpsSerial.begin(115200, SERIAL_8N1, 16, 17); // RX = 16, TX = 17

  setupWiFi();
  WiFi.setSleep(false); // Disable WiFi sleep mode for continuous data reception
  startWebServer();
  startTCPServer();
}

void loop() {
  webServer.handleClient();
  bool newClient = false;
  char buffer[BUFFER_SIZE];
  int index = 0;

  WiFiClient client = gpsTcpServer.available();
  
  if (client && client.connected()) {
    Serial.println("Client connected");
    client.setNoDelay(true); // Disable Nagle's algorithm for low latency

    while (client.connected()) {      
      while (gpsSerial.available()) {
        char c = gpsSerial.read();
        buffer[index++] = c;
        if (c == '\n' || index >= BUFFER_SIZE - 1) {
          client.write((const uint8_t*)buffer, index);
          index = 0;
          // client.flush();
        }
      }
      delay(1); // Allow time for incoming data
    }
    client.stop();
    Serial.println("Client disconnected");
  }
}
