// Defina os pinos de LED e LDR
// Defina uma variável com valor máximo do LDR (4000)
// Defina uma variável para guardar o valor atual do LED (10)
int ledPin = 13;
int ledValue = 10;

int ldrPin = 34;
// Faça testes no sensor ldr para encontrar o valor maximo e atribua a variável ldrMax
int ldrMax = 300;

void setup() {
    Serial.begin(9600);
    
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    
    Serial.printf("SmartLamp Initialized.\n");


}

// Função loop será executada infinitamente pelo ESP32
void loop() {
  delay(1000);
  int ldrVal = ldrGetValue();
  Serial.printf("Ldr Value %d\n",ldrVal);
  digitalWrite(ledPin,HIGH);
  delay(1000);
  digitalWrite(ledPin,LOW);
    //Obtenha os comandos enviados pela serial 
    //e processe-os com a função processCommand
}


void processCommand(String command) {

}

// Função para atualizar o valor do LED
void ledUpdate() {
    // Valor deve convertar o valor recebido pelo comando SET_LED para 0 e 255
    // Normalize o valor do LED antes de enviar para a porta correspondente
}

// Função para ler o valor do LDR
int ldrGetValue() {
    float ldrVal = analogRead(ldrPin);
    ldrVal = (ldrVal/ldrMax)*100;
    return (int)ldrVal;
}
