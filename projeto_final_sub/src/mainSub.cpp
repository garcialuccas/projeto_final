#include <Keypad.h>
#include <Bounce2.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include "internet.h"

#define pinApagar 13
#define pinEnviar 34
#define pinLed 23

const String numEsp = "1";

const unsigned long tempoEsperaConexao = 10000;
const unsigned long tempoEsperaReconexao = 5000;

const char *mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
const char *mqtt_client_id = "senai134_esp1_sub_match_game";
const char *mqtt_topic_sub = "main_match_game_pub";
const char *mqtt_topic_pub = "main_match_game_sub";

const int botaoAz = 19;
int estadoBotaoAnterior = HIGH;

WiFiClient espClient;
PubSubClient client(espClient);

LiquidCrystal_I2C lcd(0x27, 20, 4);

Bounce botaoApagar = Bounce();
Bounce botaoEnviar = Bounce();

unsigned long tempoIniciar = 0;

void conectaMqtt();
void retornoMqtt(char *, byte *, unsigned int);
void conectado();
void pronto();
void enviarResposta();

#define ROW_NUM     4 // quatro linhas
#define COLUMN_NUM  3 // tres colunas

char keys[ROW_NUM][COLUMN_NUM] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

byte pin_rows[ROW_NUM] = {26, 25, 33, 32}; // conexao dos pinos das linhas
byte pin_column[COLUMN_NUM] = {12, 14, 27};  // conexao dos pinos das colunas

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM );

// variaveis para guardar se o esp está pronto ou não
bool iniciar = false;
bool iniciarAnterior = false;

// variavel para guardar a resposta durante o código
String resposta;

void setup() {

  pinMode(pinLed, OUTPUT);

  botaoApagar.attach(pinApagar, INPUT_PULLUP);
  botaoEnviar.attach(pinEnviar, INPUT_PULLUP);

  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(retornoMqtt);
  digitalWrite(pinLed, 1);

}

void loop() {

  botaoApagar.update();
  botaoEnviar.update();

  checkWiFi();

  client.loop();

  if (!client.connected()) {
    conectaMqtt();
  }

  char key = keypad.getKey();

  if (iniciar && !iniciarAnterior) {
      if (millis() - tempoIniciar > 1000) { 
        lcd.clear();
        iniciarAnterior = true;
      }
  }

  if (isdigit(key) && iniciar) {
    resposta.concat(key);
    lcd.setCursor(0, 0);
    lcd.print(resposta.c_str());
  }

  if (botaoEnviar.fell() && !iniciar) {
    pronto();
    Serial.print("iniciar");
  }

  if (botaoEnviar.fell() && iniciar) {
    enviarResposta();
    resposta = "";
    lcd.clear();
  }

  if (botaoApagar.fell() && iniciar) {
    resposta = resposta.substring(0, resposta.length() - 1);
    lcd.setCursor(resposta.length(), 0);
    lcd.print(" ");
    Serial.println(resposta);
  }

}

void conectaMqtt()
{
  if (!client.connected())
  {
    Serial.println("Conectando ao MQTT...");

    if (client.connect(mqtt_client_id))
    {
      Serial.println("MQTT conectado!");
      client.subscribe(mqtt_topic_sub);
      conectado();
    }
    else
    {
      Serial.print("Falha MQTT: ");
      Serial.print(client.state());
    }
  }
}

void retornoMqtt(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Msg recebida: ");
  Serial.println(topic);

  String mensagem;
  for (int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  Serial.println(mensagem);

  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, mensagem);

  if (erro) Serial.println("erro ao ler json");

  const char *fim = doc["fim"];

  if (fim != NULL) {

    if (strcmp(fim, "0") == 0) {
      iniciar = true;
      iniciarAnterior = false;
      tempoIniciar = millis();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("VAMOS NESSA!");
    }
    if (strcmp(fim, "1") == 0) {
      iniciar = false;
      conectado();
    }
  }
}

void conectado() {

  JsonDocument doc;
  String msg;

  doc["esp"] = "esp" + numEsp;
  doc["msg"] = "conectado";
  doc["conectado"] = "1";
  doc["iniciar"] = "0";
  doc["resposta"] = "0";

  serializeJson(doc, msg);
  client.publish(mqtt_topic_pub, msg.c_str());

  lcd.clear();
  lcd.print("iniciar?");

}

void pronto() {
  JsonDocument doc;
  String msg;

  doc["esp"] = "esp" + numEsp;
  doc["msg"] = "estou pronto";
  doc["conectado"] = "1";
  doc["iniciar"] = "1";
  doc["resposta"] = "0";

  serializeJson(doc, msg);
  client.publish(mqtt_topic_pub, msg.c_str());

  lcd.clear();
  lcd.print("pronto!");

}

void enviarResposta() {

  JsonDocument doc;
  String msg;

  doc["esp"] = "esp" + numEsp;
  doc["msg"] = "enviando resposta";
  doc["conectado"] = "1";
  doc["iniciar"] = "1";
  doc["resposta"] = resposta;

  serializeJson(doc, msg);
  client.publish(mqtt_topic_pub, msg.c_str());

}