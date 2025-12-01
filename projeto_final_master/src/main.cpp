#include <Arduino.h>
#include "internet.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char *mqtt_client_id = "senai134_esp_main_match_game";
const char *mqtt_topic_sub = "main_match_game_sub";
const char *mqtt_topic_pub = "main_match_game_pub";
String esp1, esp2, esp3;
String mensagem1, mensagem2, mensagem3;
bool iniciar1, iniciar2, iniciar3;
unsigned long tempo1 = 999999999;
unsigned long tempo2 = 999999999;
unsigned long tempo3 = 999999999;
float resposta1, resposta2, resposta3;

String cores[] = {"vermelho", "verde", "azul"};
// 1 = vermelho
// 2 = verde
// 3 = azul

float resposta = 0;
int perguntaAtual = 0;

bool ninguem = true;
bool gerados = false;
bool respondida = true;
float x, y;

WiFiClient espClient;
PubSubClient client(espClient);

int jogadores = 0;
const int maxPerguntas = 10;
String pontos[maxPerguntas] = {};

LiquidCrystal_I2C lcd(0x27, 20, 4);

//PROTOTIPO DAS FUNÇÕES
void conectaMqtt();
void retornoMqtt(char *, byte *, unsigned int);
float gerarResposta(float n, float m);

void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(10000);

  lcd.init();
  lcd.backlight();
  lcd.print("INICIAR?");
  lcd.setCursor(0, 1);
  lcd.print("Jogadores: 0");

  conectaWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(retornoMqtt);
}

void loop()
{
  checkWiFi();

  client.loop(); // Atualiza o cliente

  if (!client.connected()) // Verifica se não esta conectado
  {
    conectaMqtt(); // Executa a função de conectar
  }

  if (!(jogadores == iniciar1 + iniciar2 + iniciar3) || ninguem) {
    lcd.setCursor(11, 1);
    lcd.print(jogadores);
    return;
  }

  if (!gerados) {
    x = random(0, 11);
    y = random(0, 11);
    resposta = gerarResposta(x, y);
    gerados = true;
    respondida = false;
  }

  if (!respondida) {

    float respostas[] = {resposta1, resposta2, resposta3};
    unsigned long tempos[] = {tempo1, tempo2, tempo3};

    for (int i = 0; i < 3; i++) {
      if (respostas[i] == resposta && tempos[i] == min(tempo1, min(tempo2, tempo3))) {
        pontos[perguntaAtual] = cores[i];
        respondida = true;
        gerados = false;
      }
    }

    resposta1 = 0;
    resposta2 = 0;
    resposta3 = 0;
  }

  // ---------------- LEITURA DA SERIAL ----------------
  if (Serial.available() > 0)
  {
    String textoDigitado = Serial.readStringUntil('\n');
    textoDigitado.trim();

    // ---------------- NAO ENVIA TEXTO VAZIO ----------------
    if (textoDigitado.length() == 0)
    {
      Serial.println("Mensagem vazia");
      return;
    }
    // ---------------- ESTRUTURA O JSON ----------------
    JsonDocument doc;
    doc["disp"] = "Esp32MasterMatchGame";
    doc["msg"] = textoDigitado;
    doc["time"] = millis();

    String stringJson;
    serializeJson(doc, stringJson);

    // ---------------- PUBLICA NO MQTT ----------------
    client.publish(mqtt_topic_pub, stringJson.c_str());
    Serial.print("Enviado: ");
    Serial.println(stringJson);
  }
}

void conectaMqtt()
{
  while (!client.connected())
  {
    Serial.println("Conectando ao Mqtt...");
    if (client.connect(mqtt_client_id))
    {
      Serial.println("Conectado");
      client.subscribe(mqtt_topic_sub);
    }
    else
    {
      Serial.print("Falha :");
      Serial.print(client.state());
      Serial.print("Tentando novamente em 5s.");
      delay(5000);
    }
  }
}

void retornoMqtt(char *topic, byte *payload, unsigned int length)
{

  Serial.print("Mensagem recebida em: ");
  Serial.print(topic);
  Serial.print(": ");

  // ---------------- MONTA A STRING COM A MENSAGEM RECEBIDA ----------------
  String mensagemRecebida = "";
  for (int i = 0; i < length; i++)
  {
    mensagemRecebida += (char)payload[i];
  }

  Serial.print("JSON recebido: ");
  Serial.println(mensagemRecebida);

  // ---------------- INTERPRETA JSON RECEBIDO ----------------
  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, mensagemRecebida);

  if (erro)
  {
    Serial.print("Erro ao decodificar JSON: ");
    Serial.print(erro.c_str());
    return;
  }

  // ---------------- ACESSA OS CAMPOS JSON ----------------
  const char *disp = doc["disp"];
  const char *msg = doc["msg"];
  const char* ini = doc["iniciar"];
  const char* time = doc["tempo"];
  const char* resp = doc["resposta"];
  const char* primeira = doc["primeira"];

  bool b = bool(ini);
  unsigned long t = long(time);
  float r = String(resp).toFloat();
  
  if (strcmp(disp, "esp1") == 0) {
    esp1 = disp;
    mensagem1 = msg;
    iniciar1 = b;
    tempo1 = t;
    resposta1 = r;
    if (String(primeira).equals("1")) jogadores++; 
    ninguem = false;
  }

  else if (strcmp(disp, "esp2") == 0) {
    esp2 = disp;
    mensagem2 = msg;
    iniciar2 = b;
    tempo2 = t;
    resposta2 = r;
    if (String(primeira).equals("1")) jogadores++; 
    ninguem = false;
  }

  else if (strcmp(disp, "esp3") == 0) {
    esp3 = disp;
    mensagem3 = msg;
    iniciar3 = b;
    tempo3 = t;
    resposta3 = r;
    if (String(primeira).equals("1")) jogadores++; 
    ninguem = false;
  }

  Serial.println("Dados recebidos");
  Serial.print("Dispositivo: ");
  Serial.println(disp);
  Serial.print("Mensagem: ");
  Serial.println(msg);
  Serial.print("tempo: ");
  Serial.println(time);
  Serial.print("Iniciar:");
  Serial.println(ini);
  Serial.print("Resposta:");
  Serial.println(resp);
  Serial.println("-------------------------");
}

float gerarResposta(float n, float m) {

  int alternativa = random(0, 4);
  float r = 0;
  String operacoes[] = {"+", "-", "*", "/"};

  lcd.clear();

  if (m == 0 && alternativa == 3) alternativa--;

  else {
    if (alternativa == 0) r = n + m;
    else if (alternativa == 1) r = n - m;
    else if (alternativa == 2) r = n * m;
    else if (alternativa == 3) r = n / m;
  }

  lcd.printf("%f %s %f = ??", n, operacoes[alternativa], m);

  return r;

}

void imprimirResposta
() {}