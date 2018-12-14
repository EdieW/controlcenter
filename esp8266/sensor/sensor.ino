#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>
#include <SimpleDHT.h>

// for DHT11,
//      VCC: 5V or 3V
//      GND: GND
//      DATA: 2
int pinDHT11 = 2;
SimpleDHT11 dht11(pinDHT11);

//replay control pin - D1
int relayInput = 5; // the input to the relay pin
int relayon = 0;
float temp_on = 30;

#define TCP             0
#define UDP             1
#define DS18B20_ENAB    1
#define DHT11_ENAB      0
#define MOISTURE        1

#if DHT11_ENAB
int humid_on = 50;
#else
int humid_on = 40;
#endif

//------------------------------------------
//DS18B20
#define ONE_WIRE_BUS D3 //Pin to which is attached a temperature sensor
#define ONE_WIRE_MAX_DEV 15 //The maximum number of devices

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
int numberOfDevices; //Number of temperature devices found
DeviceAddress devAddr[ONE_WIRE_MAX_DEV];  //An array device temperature sensors
float tempDev[ONE_WIRE_MAX_DEV]; //Saving the last measurement of temperature
float tempDevLast[ONE_WIRE_MAX_DEV]; //Previous temperature measurement
long lastTemp; //The last measurement
const int durationTemp = 3000; //The frequency of temperature measurement

//------------------------------------------
//WIFI
const char* ssid = "ATT9442";
const char* password = "maplehome123";

//------------------------------------------
//HTTP
ESP8266WebServer server(80);

//TCP
WiFiClient client;
const char *host = "192.168.10.119";
const uint16_t port = 17000;

//UDP
WiFiUDP Udp;
unsigned int localUdpPort = 17001;
unsigned int remoteUdpPort = 17002;

void relay(int on)
{
   if (on) {
    digitalWrite(relayInput, HIGH);
   } else {
    digitalWrite(relayInput, LOW);
   }
   relayon = on;
}

void TurnOnRelay(float temp, int humid) {
  if (temp > temp_on || humid < humid_on)
     relay(1);
  else
     relay(0);
}

#if TCP
//TCPConnect
void TCPConnect(const char *host, uint16_t port)
{
  if (!client.connected()) {
    if (!client.connect(host, port)) {
      Serial.println("TCP connection failed");
    } else {
      Serial.println("TCP connection up");
    }
  }

}

void TCPSend(char *data)
{
  // This will send a string to the server
  if (client.connected()) {
    Serial.print("sending data to server:");
    Serial.println(data);

    client.println(data);
  }
}

int TCPReceive(char *data, int len)
{
  int num = 0;
  // Read all the lines of the reply from server and print them to Serial
  while (client.available() && num < len) {
    data[num] = static_cast<char>(client.read());
    Serial.print(data[num]);
    num++;
  }

  return num;
}

void TCPClose()
{
  // Close the connection
  Serial.println("closing connection");
  client.stop();
}
#endif

#if UDP
int UdpSend(const char* hostip, int port, char* replyPacket)
{
  int ret = -1;
  if (Udp.beginPacket(hostip, port)) {
    Udp.write(replyPacket);
    ret = Udp.endPacket();
  }
  return ret;
}

int UdpReceive(char *data, int len)
{
  int packetSize = Udp.parsePacket();

  if (packetSize)
  {
    // receive incoming UDP packets
    Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    if (packetSize > len-1)
      packetSize = len-1;

    len = Udp.read(data, packetSize);
    if (len > 0)
    {
      data[len] = 0;
    }
    packetSize = len;
  }

  return packetSize;
}

int UdpReply(char *data)
{
    // send back a reply, to the IP address and port we got the packet from
    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    Udp.write(data);
    return Udp.endPacket();
}
#endif

//DHT11
int DHT11_Get_Temp(float *temp, int *humid) {
  // read without samples.
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT11 failed, err="); Serial.println(err);
    return -1;
  }

  Serial.print("Sample OK: ");
  Serial.print((int)temperature); Serial.print(" *C, ");
  Serial.print((int)humidity);
  Serial.print(" H, Relay ");
  Serial.println((int)relayon);

  *temp = (float)temperature;
  *humid = (int)humidity;
  return 0;
}

//------------------------------------------
//Convert device id to String
String GetAddressToString(DeviceAddress deviceAddress){
  String str = "";
  for (uint8_t i = 0; i < 8; i++){
    if( deviceAddress[i] < 16 ) str += String(0, HEX);
    str += String(deviceAddress[i], HEX);
  }
  return str;
}

//Setting the temperature sensor
void SetupDS18B20(){
  DS18B20.begin();

  Serial.print("Parasite power is: ");
  if( DS18B20.isParasitePowerMode() ){
    Serial.println("ON");
  }else{
    Serial.println("OFF");
  }

  numberOfDevices = DS18B20.getDeviceCount();
  Serial.print( "Device count: " );
  Serial.println( numberOfDevices );

  lastTemp = millis();
  DS18B20.requestTemperatures();

  // Loop through each device, print out address
  for(int i=0;i<numberOfDevices; i++){
    // Search the wire for address
    if( DS18B20.getAddress(devAddr[i], i) ){
      //devAddr[i] = tempDeviceAddress;
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: " + GetAddressToString(devAddr[i]));
      Serial.println();
    }else{
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }

    //Get resolution of DS18b20
    Serial.print("Resolution: ");
    Serial.print(DS18B20.getResolution( devAddr[i] ));
    Serial.println();

    //Read temperature from DS18b20
    float tempC = DS18B20.getTempC( devAddr[i] );
    Serial.print("Temp C: ");
    Serial.println(tempC);
  }
}

//Loop measuring the temperature
int TempLoop(float *temp){
  char str[200];

  memset(str, 0, sizeof(str));
  for(int i=0; i<numberOfDevices; i++){
    char buf[40];
    float tempC = DS18B20.getTempC( devAddr[i] ); //Measuring temperature in Celsius
    tempDev[i] = tempC; //Save the measured value to the array
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "Temp %d: %f\n", i, tempC);
    Serial.println(buf);
    strcat(str, buf);
    *temp = tempC;
  }

  DS18B20.setWaitForConversion(false); //No waiting for measurement
  DS18B20.requestTemperatures(); //Initiate the temperature measurement

  return 0;
}

//------------------------------------------
void HandleRoot(){
  String message = "Number of devices: ";
  message += numberOfDevices;
  message += "\r\n<br>";
  char temperatureString[6];

  message += "<table border='1'>\r\n";
  message += "<tr><td>Device id</td><td>Temperature</td></tr>\r\n";
  for(int i=0;i<numberOfDevices;i++){
    dtostrf(tempDev[i], 2, 2, temperatureString);
    Serial.print( "Sending temperature: " );
    Serial.println( temperatureString );

    message += "<tr><td>";
    message += GetAddressToString( devAddr[i] );
    message += "</td>\r\n";
    message += "<td>";
    message += temperatureString;
    message += "</td></tr>\r\n";
    message += "\r\n";
  }
  message += "</table>\r\n";

  server.send(200, "text/html", message );
}

void HandleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}

int calculatemoi(int newval)
{
   char str[50];
   int value;

   // 1024 is dryest value, 550 is wetest value
   value = map(newval,1024,550,0,100);
   Serial.println(value);

   sprintf(str, "rawdata %d humid %d", newval, value);
   Serial.println(str);

   return value;
}
//------------------------------------------
void setup() {
  int i = 0;
  //Setup Serial port speed
  Serial.begin(115200);

  pinMode(relayInput, OUTPUT); // initialize pin as OUTPUT
  relay(0);

  //Setup WIFI
  WiFi.begin(ssid, password);
  Serial.println("");

  //Wait for WIFI connection
  while (WiFi.status() != WL_CONNECTED && i++< 20) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HandleRoot);
  server.onNotFound( HandleNotFound );
  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() );

#if DS18B20_ENAB
  //Setup DS18b20 temperature sensor
  SetupDS18B20();
#endif

#if TCP
  TCPConnect(host,port);
#endif

#if UDP
  //listen on localUdpPort
  Udp.begin(localUdpPort);
#endif

}

void loop() {
  float temp;
  int humid = 0;
  int ret = 0;

  server.handleClient();
#if TCP
  TCPConnect(host, port);
#endif
#if DS18B20_ENAB
  ret = TempLoop( &temp );
#endif
#if DHT11_ENAB
  ret = DHT11_Get_Temp(&temp, &humid);
#endif
#if MOISTURE
  //Serial.println(analogRead(A0));
  humid = calculatemoi(analogRead(A0));
#endif
  if (!ret) {
    //report info
    char str[100];
    memset(str, 0, sizeof(str));
    TurnOnRelay(temp, humid);
    sprintf(str, "%f,%d,%d", temp, humid, relayon);
#if TCP
    TCPSend(str);
#endif
#if UDP
    ret = UdpSend(host, remoteUdpPort, str);
    if (ret == 1) {
      //transmit ok
      delay(500);
      ret = UdpReceive(str, sizeof(str));
      if (ret > 0) {
        Serial.print("Udp Rxed: ");
        Serial.println(str);
        if (!strcmp(str, "OK"))
          Serial.println("Got Ack");
        else if (str[0] == 'T') {
          temp_on = atof(&str[1]);
        } else if (str[0] == 'H') {
          humid_on = atoi(&str[1]);
        }
      }
    }
#endif
  }

  delay(durationTemp);
}
