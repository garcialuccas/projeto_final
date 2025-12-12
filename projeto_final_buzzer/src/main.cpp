#include <Arduino.h>
#include <WiFi.h>
#include "internet.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"

#define pinoBuzzer 12

#define analogMotorDireita 2
#define analogMotorEsquerda 4

#define frenteMotorDireita 18
#define atrasMotorDireita 19
#define frenteMotorEsquerda 33
#define atrasMotorEsquerda 32

#define LED_R 13
#define LED_B 5

const char *mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
const char *mqtt_client_id = "senai134_esp2_buzzer_match_game";
const char *mqtt_topic_sub = "main_match_game_pub";
const char *mqtt_topic_pub = "main_match_game_sub";

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

bool vencedorDetectado = false;
bool iniciar = 0;
bool iniciarAnterior = 0;
bool voltarMotor = false;
unsigned long tempoMotor = 0;

String vencedor = "";

int pontosAzul = 0;
int velocidadeAzul = 0;
int pontosVermelho = 0;
int velocidadeVermelho = 0;

const int freq = 30000;
const int canalPWM_Direita = 2;
const int canalPWM_Esquerda = 0;
const int resolution = 8;

// ---------------- NOTAS ----------------
const int FREQ_MI1 = 660; 
const int FREQ_RE1 = 588;
const int FREQ_DO1 = 524;
const int FREQ_FA1 = 698;
const int FREQ_SOL1 = 784;

struct Nota {
  int frequencia;
  int duracao; 
  int pausa;   
};

Nota musica [] = {
   {FREQ_MI1, 150, 50},  
   {FREQ_MI1, 180, 50},  
   {FREQ_RE1, 1200, 150},
   {FREQ_RE1, 150, 50},  
   {FREQ_RE1, 180, 50},  
   {FREQ_DO1, 1200, 150}, 
   {FREQ_MI1, 150, 50},  
   {FREQ_MI1, 180, 50},  
   {FREQ_RE1, 1200, 130}, 
   {FREQ_RE1, 150, 50},  
   {FREQ_RE1, 180, 50},  
   {FREQ_DO1, 1200, 150}, 
};

const int NOTAAS = sizeof(musica) / sizeof(musica[0]);

// ---------------- FUNÇÃO MÚSICA FINAL ----------------
void musicafinal() {
  for (int i = 0; i < NOTAAS; i++) {
    tone(pinoBuzzer, musica[i].frequencia, musica[i].duracao);
    delay(musica[i].duracao + musica[i].pausa);
    noTone(pinoBuzzer);
  }
}

// ---------------- PISCAR LED RGB DO VENCEDOR ----------------
void piscarLeds(String cor) {
  for (int i = 0; i < 5; i++) {
    if (cor == "azul") {
      digitalWrite(LED_B, HIGH);
    } else if (cor == "vermelho") {
      digitalWrite(LED_R, HIGH);
    }
    delay(300);
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_B, LOW);
    delay(300);
  }
}

// ---------------- SENSOR DISTÂNCIA ----------------
void verificarSensor() {
  if (vencedorDetectado) return;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    int distancia = measure.RangeMilliMeter;

    if (distancia < 200 && distancia > 30) {
      vencedorDetectado = true;
      iniciar = false;
      piscarLeds("azul");
      vencedor = "azul";
      voltarMotor = true;
    }
    else if (distancia < 400 && distancia > 30) {
      vencedorDetectado = true;
      iniciar = false;
      piscarLeds("vermelho");
      voltarMotor = true;
    }

    if (voltarMotor) {

      digitalWrite(frenteMotorDireita, LOW);
      digitalWrite(frenteMotorEsquerda, LOW);

      ledcWrite(canalPWM_Direita, 255);
      ledcWrite(canalPWM_Esquerda, 255);

      digitalWrite(atrasMotorDireita, HIGH);
      digitalWrite(atrasMotorEsquerda, HIGH);

      tempoMotor = millis();
      musicafinal();
      voltarMotor = false;

    }
  }
}

// ---------------- CONETA MQTT ----------------
void conectaMqtt() {
  while (!client.connected()) {
    Serial.println("Conectando ao Mqtt...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("Conectado");
      client.subscribe(mqtt_topic_sub);
    } else {
      Serial.print("Falha :");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 5s.");
      delay(5000);
    }
  }
}

void retornoMqtt(char *topic, byte *payload, unsigned int length) {

  Serial.print("Mensagem recebida em: ");
  Serial.print(topic);
  Serial.print(": ");

  String mensagemRecebida = "";
  for (int i = 0; i < length; i++)
  {
    mensagemRecebida += (char)payload[i];
  }

  Serial.print("JSON recebido: ");
  Serial.println(mensagemRecebida);

  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, mensagemRecebida);

  if (erro)
  {
    Serial.print("Erro ao decodificar JSON: ");
    Serial.print(erro.c_str());
    return;
  }

  // save dos valores recebidos
  const char *esp = doc["esp"];
  const char *pont = doc["pontos"];
  const char *fim = doc["fim"];

  // tratamento e atribuição dos valores recebidos
  if (strcasecmp(esp, "esp1") == 0) {
    pontosAzul++;
    if (pontosAzul >= 10) pontosAzul = 10;
    velocidadeAzul = map(pontosAzul, 0, 10, 0, 255);
  }
  else if (strcasecmp(esp, "esp2") == 0) {
    pontosVermelho++;
    if (pontosVermelho >= 10) pontosVermelho = 10;
    velocidadeVermelho = map(pontosVermelho, 0, 10, 0, 255);
  }

  if (strcasecmp(fim, "0") == 0) iniciar = true;
  else if (strcasecmp(fim, "1") == 0) iniciar = false;
}

void setup() {

  pinMode(pinoBuzzer, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_B, OUTPUT);

  ledcSetup(canalPWM_Direita, freq, resolution);
  ledcSetup(canalPWM_Esquerda, freq, resolution);
  
  ledcAttachPin(analogMotorDireita, canalPWM_Direita);
  ledcAttachPin(analogMotorEsquerda, canalPWM_Esquerda);

  Serial.begin(115200);

  conectaWiFi();

  pinMode(frenteMotorDireita, OUTPUT);
  pinMode(atrasMotorDireita, OUTPUT);
  pinMode(frenteMotorEsquerda, OUTPUT);
  pinMode(atrasMotorEsquerda, OUTPUT);

  if (!lox.begin()) {
    Serial.println("Erro ao iniciar VL53L0X");
    while(1);
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(retornoMqtt);

}

void loop() {
  
  checkWiFi();
  
  client.loop();

  if (!client.connected()) {
    conectaMqtt();
  }

  if (iniciar) {

    verificarSensor();

    ledcWrite(analogMotorDireita, velocidadeAzul);
    ledcWrite(analogMotorEsquerda, velocidadeVermelho);
    digitalWrite(frenteMotorDireita, HIGH);
    digitalWrite(frenteMotorEsquerda, HIGH);
  }

  if (!iniciar && vencedorDetectado) {
    JsonDocument doc;
    String msg;

    doc["vencedor"] = vencedor;

    serializeJson(doc, msg);
    client.publish(mqtt_topic_pub, msg.c_str());

    if (millis() - tempoMotor >= 500) {
      digitalWrite(atrasMotorDireita, LOW);
      digitalWrite(atrasMotorEsquerda, LOW);
    }

    vencedorDetectado = false;

  }

  Serial.println("rodar");
  delay(2000);

}