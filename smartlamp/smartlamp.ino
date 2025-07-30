#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>


#define DHTPIN 14 
#define DHTTYPE    DHT11     // DHT 11
float temp = 0;
float hum = 0;
DHT_Unified dht(DHTPIN, DHTTYPE);


// Defina os pinos de LED e LDR
// Defina uma variável com valor máximo do LDR (4000)
// Defina uma variável para guardar o valor atual do LED (10)
int ledPin = 5;
int ledValue = 10;

int ldrPin = A0;
// Faça testes no sensor ldr para encontrar o valor maximo e atribua a variável ldrMax
int ldrMax = 1024;

int ledVal = 10;

const String SET_LED = "SET_LED";
const String GET_LED = "GET_LED";
const String GET_LDR = "GET_LDR";
const String GET_TEMP = "GET_TEMP";
const String GET_HUM = "GET_HUM";



// Função para ler o valor do LDR
int ldrGetValue() {
    float ldrVal = analogRead(ldrPin);
    ldrVal = (ldrVal/ldrMax)*100;
    if (ldrVal > 100)
        ldrVal = 100;
    return (int)ldrVal;
}


void readDht11() {
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  temp = event.temperature;

  dht.humidity().getEvent(&event);
  hum = event.relative_humidity;
}

int getLedNormalizedVal(int val) {
    return ((float)val/100)*255;
}

// Função para atualizar o valor do LED
void ledUpdate(int ledInt) {
    // Valor deve convertar o valor recebido pelo comando SET_LED para 0 e 255
    // Normalize o valor do LED antes de enviar para a porta correspondente
    if (ledInt >= 0 && ledInt <= 100) {
        ledVal = ledInt;
        analogWrite(ledPin,getLedNormalizedVal(ledVal));
        Serial.printf("RES SET_LED 1\n");
    } else {
        Serial.printf("RES SET_LED -1\n");
    }
}

void processCommand(String command) {
    String cmd;
    cmd = command.substring(0,7);
    int firstSpaceIndex = command.indexOf(' ');

    if (firstSpaceIndex != -1) {
        cmd = command.substring(0, firstSpaceIndex);
    } else {
    // If there's no space, the whole string is considered one word
        cmd = command;
    }
    if (cmd == GET_LDR) {
        Serial.printf("RES GET_LDR %d\n", ldrGetValue());
        return;
    } else if (cmd == GET_LED) {
        Serial.printf("RES GET_LED %d\n", ledVal);
        return;
    } else if (cmd == GET_TEMP) {
        readDht11();
        Serial.printf("RES GET_TEMP %.0f\n", temp);
        return;
    } else if (cmd == GET_HUM) {
        readDht11();
        Serial.printf("RES GET_HUM %.0f\n", hum);
        return;
    } else if (cmd == SET_LED && command.length() >= 9) {
        String val = command.substring(8);
        int ledInt = val.toInt();
        ledUpdate(ledInt);
        return;
    }
    Serial.printf("ERR Unknown command.\n");
}

void setup() {
    Serial.begin(9600);

    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    analogWrite(ledPin,getLedNormalizedVal(ledVal));
    processCommand(GET_LDR);
    Serial.printf("SmartLamp Initialized.\n");
}

// Função loop será executada infinitamente pelo ESP32
void loop() {
    //Obtenha os comandos enviados pela serial 
    //e processe-os com a função processCommand
    //processCommand(GET_LDR);
    if (Serial.available() > 0) {
        String command = Serial.readString();
        command.trim();
        processCommand(command);
    }
    delay(100);
}