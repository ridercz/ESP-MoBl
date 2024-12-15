#include <Arduino.h>          // Include the Arduino core library
#include <ESP8266WiFi.h>      // Include the ESP8266 WiFi library
#include <ESP8266WebServer.h> // Include the ESP8266 web server library
#include <DNSServer.h>        // Include the DNS server library
#include <LittleFS.h>         // Include the LittleFS library
#include "html.h"             // Include the HTML markup header file

// Configuration /////////////////////////////////////////////////////////////////////////////////////////////////////

#define VERSION "1.0"                     // Firmware version
#define WIFI_AP_SSID "ESP-MOBL-%08X"      // Access point name template
#define WIFI_AP_PASS "TopSecret"          // Access point password
#define WIFI_AP_HOST "morseblinker.local" // Access point hostname
#define WIFI_AP_IP 192, 168, 1, 1         // Access point IP address
#define HTTP_PORT 80                      // HTTP server TCP port
#define DNS_PORT 53                       // DNS server TCP port
#define LED_EXTERNAL D2                   // External LED pin
#define LED_INTERNAL D0                   // Built-in LED pin
#define DATA_FILENAME "/data.txt"         // File name to store text
#define DEFAULT_TEXT "HELLO WORLD"        // Default text to show
#define MORSE_DOT_DURATION 100            // Duration of a dot in milliseconds
#define MORSE_DASH_DURATION 400           // Duration of a dash in milliseconds
#define MORSE_PAUSE_DURATION 200          // Duration of a pause between dots and dashes in milliseconds
#define MORSE_LETTER_PAUSE_DURATION 1000  // Duration of a pause between letters in milliseconds
#define MORSE_WORD_PAUSE_DURATION 1500    // Duration of a pause between words in milliseconds
#define MORSE_MESSAGE_PAUSE_DURATION 2000 // Duration of a pause between loop in milliseconds

// Global variables //////////////////////////////////////////////////////////////////////////////////////////////////

DNSServer dnsServer;                   // DNS server object
ESP8266WebServer webServer(HTTP_PORT); // HTTP server object
char ssid[20];                         // Access point name
String clearText = "";                 // Text to blink in Morse code
String morseText = "";                 // Morse code of the text
unsigned int morseIndex = 0;           // Index of the current Morse code character

// Function prototypes ///////////////////////////////////////////////////////////////////////////////////////////////

void setLed(bool state, int duration = 0); // Set LED state and wait
void handleRoot();                         // Handle HTTP request to '/'
void handleSetupCgi();                     // Handle HTTP request to '/blink.cgi'
void handleNotFound();                     // Handle HTTP request to unknown path
String textToMorse(String text);           // Convert text to Morse code
String charToMorse(char c);                // Convert character to Morse code

// Main code /////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup()
{
  // Initialize serial communication
  Serial.begin(9600);
  Serial.println();
  Serial.println();
  Serial.println("-------------------------------------------------------------------------------");
  Serial.println("ESP-MOBL/" VERSION " (Morse Blinker)");
  Serial.println("Copyright (c) Michal Altair Valasek, 2024 | MIT License");
  Serial.println("              www.rider.cz | github.com/ridercz/ESP-MOBL");
  Serial.println("-------------------------------------------------------------------------------");

  // Set up the LED pins as output
  pinMode(LED_EXTERNAL, OUTPUT); // Initialize external LED pin
  pinMode(LED_INTERNAL, OUTPUT); // Initialize built-in LED pin
  setLed(HIGH);                  // Turn on LEDs for initialization

  // Initialize LittleFS in flash memory
  Serial.print("Initializing LittleFS...");
  if (!LittleFS.begin())
  {
    // Unable to initialize LittleFS, probably incorrect flash partitioning
    Serial.println("ERROR!");
    Serial.println("Please check your flash partitioning in platformio.ini");
    while (true)
      ;
  }
  if (LittleFS.exists(DATA_FILENAME))
  {
    // Read text from file
    File file = LittleFS.open(DATA_FILENAME, "r");
    clearText = file.readString();
    file.close();
    Serial.println("OK, read text from file:");
  }
  else
  {
    // Format LittleFS
    Serial.print("Formatting...");
    LittleFS.format();

    // Create file with default text
    File file = LittleFS.open(DATA_FILENAME, "w");
    file.print(DEFAULT_TEXT);
    file.close();
    clearText = DEFAULT_TEXT;
    Serial.println("OK, created file with default text:");
  }

  // Print text and Morse code
  Serial.println(clearText);
  Serial.println("Morse code:");
  morseText = textToMorse(clearText);
  Serial.println(morseText);

  // Create WiFi access point
  Serial.print("Creating WiFi access point...");
  sprintf(ssid, WIFI_AP_SSID, ESP.getChipId());
  IPAddress apIp(WIFI_AP_IP); // IP address of the access point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, WIFI_AP_PASS);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  Serial.println("OK");
  Serial.print("  Network name:   ");
  Serial.println(ssid);
  Serial.println("  Password:       " WIFI_AP_PASS);
  Serial.println("  Captive portal: http://" WIFI_AP_HOST "/");

  // Initialize DNS server to resolve all names to its own IP address
  Serial.print("Initializing DNS server...");
  dnsServer.start(DNS_PORT, "*", apIp);
  Serial.println("OK, resolving all names to myself");

  // Initialize HTTP server
  Serial.print("Initializing HTTP server...");
  webServer.on("/", handleRoot);
  webServer.on("/setup.cgi", handleSetupCgi);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("OK, server running");

  // Turn off LEDs for message pause
  setLed(LOW, MORSE_MESSAGE_PAUSE_DURATION);
}

void loop()
{
  // Process DNS and HTTP requests
  dnsServer.processNextRequest();
  webServer.handleClient();

  if (morseIndex < morseText.length())
  {
    // Blink current Morse code character
    switch (morseText[morseIndex])
    {
    case '.':
      setLed(HIGH, MORSE_DOT_DURATION);
      setLed(LOW, MORSE_PAUSE_DURATION);
      break;
    case '-':
      setLed(HIGH, MORSE_DASH_DURATION);
      setLed(LOW, MORSE_PAUSE_DURATION);
      break;
    case '/':
      setLed(LOW, MORSE_LETTER_PAUSE_DURATION);
      break;
    case ' ':
      setLed(LOW, MORSE_WORD_PAUSE_DURATION);
      break;
    }
    morseIndex++;
  }
  else
  {
    // Pause between messages
    setLed(LOW, MORSE_MESSAGE_PAUSE_DURATION);
    morseIndex = 0;
  }
}

// Helper functions //////////////////////////////////////////////////////////////////////////////////////////////////

// Set LED state and wait
void setLed(bool state, int duration)
{
  // Set LED state
  digitalWrite(LED_EXTERNAL, state);  // External LED is active on high
  digitalWrite(LED_INTERNAL, !state); // Built-in LED is active on low

  if (duration > 0)
  {
    // The delay() call here is not optimal, as it blocks the HTTP and DNS server processing.
    // It would be better to use a non-blocking delay with millis().
    // However, it is good enough for this case, because we do not expect many requests and the delay is short.
    delay(duration);
  }
}

// Handle HTTP request to '/'
void handleRoot()
{
  Serial.print("Handling HTTP request to '/'...");

  String response = HTML_BEGIN;
  response += "<p>Enter text to blink in Morse code:</p>";
  response += "<form action='/setup.cgi' method='post'>";
  response += "<textarea name='text'>";
  response += clearText;
  response += "</textarea>";
  response += "<p><input type='submit' value='Submit'></p>";
  response += "</form>";
  response += "<p>Morse code:</p>";
  response += "<p>";
  response += morseText;
  response += "</p>";
  response += HTML_END;

  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Expires", "-1");
  webServer.send(200, "text/html", response);
  Serial.println("200 OK");
}

// Handle HTTP request to '/setup.cgi'
void handleSetupCgi()
{
  Serial.print("Handling HTTP request to '/setup.cgi'...");
  if (webServer.hasArg("text"))
  {
    String clearTextRaw = webServer.arg("text");

    // Remove all unsupported characters
    clearText = "";
    for (unsigned int i = 0; i < clearTextRaw.length(); i++)
    {
      char c = toUpperCase(clearTextRaw[i]);
      if (isupper(c) || isdigit(c) || c == ' ')
      {
        clearText += c;
      }
    }

    // Save text to file
    File file = LittleFS.open(DATA_FILENAME, "w");
    file.print(clearText);
    file.close();

    // Convert text to Morse code
    morseText = textToMorse(clearText);

    // Reset Morse code index
    morseIndex = 0;

    // Turn off LEDs for message pause
    setLed(LOW, MORSE_MESSAGE_PAUSE_DURATION);
  }
  webServer.sendHeader("Location", String("http://" WIFI_AP_HOST "/"), true);
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Expires", "-1");
  webServer.send(302, "text/html", "<html><body><h1>Redirecting...</h1></body></html>");
  Serial.println("302 -> http://" WIFI_AP_HOST "/");
  Serial.println("Text changed to:");
  Serial.println(clearText);
  Serial.println("Morse code:");
  Serial.println(morseText);
}

// Handle HTTP request to unknown path
void handleNotFound()
{
  String uri = webServer.uri();
  Serial.printf("Handling HTTP request to '%s'...", uri.c_str());
  webServer.sendHeader("Location", String("http://" WIFI_AP_HOST "/"), true);
  webServer.send(302, "text/html", "<html><body><h1>Redirecting...</h1></body></html>");
  Serial.println("302 -> http://" WIFI_AP_HOST "/");
}

// Convert text to Morse code
String textToMorse(String text)
{
  String morse = "";
  for (unsigned int i = 0; i < text.length(); i++)
  {
    char c = toUpperCase(text[i]);
    if (c == ' ')
    {
      morse += " /";
    }
    else
    {
      morse += charToMorse(c);
      if (i < text.length() - 1)
      {
        morse += "/";
      }
    }
  }
  return morse;
}

// Convert character to Morse code
String charToMorse(char c)
{
  switch (c)
  {
  case 'A':
    return ".-";
  case 'B':
    return "-...";
  case 'C':
    return "-.-.";
  case 'D':
    return "-..";
  case 'E':
    return ".";
  case 'F':
    return "..-.";
  case 'G':
    return "--.";
  case 'H':
    return "....";
  case 'I':
    return "..";
  case 'J':
    return ".---";
  case 'K':
    return "-.-";
  case 'L':
    return ".-..";
  case 'M':
    return "--";
  case 'N':
    return "-.";
  case 'O':
    return "---";
  case 'P':
    return ".--.";
  case 'Q':
    return "--.-";
  case 'R':
    return ".-.";
  case 'S':
    return "...";
  case 'T':
    return "-";
  case 'U':
    return "..-";
  case 'V':
    return "...-";
  case 'W':
    return ".--";
  case 'X':
    return "-..-";
  case 'Y':
    return "-.--";
  case 'Z':
    return "--..";
  case '0':
    return "-----";
  case '1':
    return ".----";
  case '2':
    return "..---";
  case '3':
    return "...--";
  case '4':
    return "....-";
  case '5':
    return ".....";
  case '6':
    return "-....";
  case '7':
    return "--...";
  case '8':
    return "---..";
  case '9':
    return "----.";
  default:
    return "";
  }
}
