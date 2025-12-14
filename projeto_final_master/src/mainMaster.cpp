// CÓDIGO DO ESP MASTER
#include <Arduino.h>
#include "internet.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// variaveis para o mqtt
const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char *mqtt_client_id = "senai134_esp_master_match_game";
const char *mqtt_topic_sub = "main_match_game_sub";
const char *mqtt_topic_pub = "main_match_game_pub";

// objetos das bibliotecas
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// estrutura do objeto jogador
struct Jogador {

  String esp;
  String mensagem;
  bool conectado;
  bool iniciar;
  float resposta;
  int pontos;
  String cor;

};

// quantidade de jogadores, jogadores conectados e jogadores prontos
const int qntdJogadores = 2;
int jogadoresConectados = 0;
int jogadoresProntos = 0;

// variavel para dizer que ninguem se conectou
bool ninguem = 1;

// array para criar e guardar os jogadores
Jogador jogadores[qntdJogadores];

int respostaVerdadeira = 0; 
int x, y;
int numeroMaximo = 10; // valor maximo do numero aleatorio gerado

// variaveis para garantir que os numeros gerados serão gerados apenas uma vez, até serem respondidos.
bool gerado = false; 
bool respondido = true;
bool enviar = false;
bool enviarAnterior = false;

// variaveis para mostrar a resposta
unsigned long tempoResposta = 0;
bool mostraResposta = false;
bool acertou = false;
int indiceResposta = 0; 

// variaveis para mostrar o vencedor
unsigned long tempoVencedor = 0;
bool vencedor = false;
String vencedorRecebido = "ninguem";

// variavel para iniciar a contagem de jogadores
bool iniciar = true;

// prototipo das funções
void conectaMqtt();
void retornoMqtt(char *, byte *, unsigned int);
void gerarResposta(int n, int m);
void mostrarVencedor();
void telaInicial();
void enviarIniciar();
void enviarFim();

void setup() {

  // gerando a pseudoaleatoriedade
  randomSeed(analogRead(36));

  // colocando os nomes de cada esp em seu respectivo jogador
  jogadores[0].esp = "esp1";
  jogadores[1].esp = "esp2";

  // colocando a cor de cada jogador
  jogadores[0].cor = "azul";
  jogadores[1].cor = "vermelho";

  // garantindo que o jogador nao vai acertar sem querer
  jogadores[0].resposta = (numeroMaximo * numeroMaximo) + 1;
  jogadores[1].resposta = (numeroMaximo * numeroMaximo) + 1;

  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  telaInicial();

  conectaWiFi();

  client.setServer(mqtt_server, mqtt_port);

  client.setCallback(retornoMqtt); 
}

void loop() {

  checkWiFi();

  client.loop();

  if (!client.connected()) conectaMqtt();

  // aguarda os jogadores conectarem e darem pronto
  if (iniciar) {
    jogadoresConectados = jogadores[0].conectado + jogadores[1].conectado;
    jogadoresProntos = jogadores[0].iniciar + jogadores[1].iniciar;

    if (jogadoresConectados == jogadoresProntos) enviar = true;
  }

  if ((jogadoresConectados != jogadoresProntos) || ninguem) {
    lcd.setCursor(11, 1);
    lcd.print(jogadoresConectados);
    return;
  }

  // inicio do jogo
  if (enviar && !enviarAnterior) {
    enviarIniciar();
    iniciar = false;
    gerado = false;
    enviar = false;
  }

  // gera os numeros aleatorios e a resposta
  if (!gerado) {
    x = random(0, numeroMaximo + 1);
    y = random(0, numeroMaximo + 1);
    gerarResposta(x, y);
    gerado = true;
    respondido = false;
  }

  // espera a resposta
  if (!respondido) {

    for (int i = 0; i < qntdJogadores; i++) {

      if (respostaVerdadeira == jogadores[i].resposta) {
        jogadores[i].pontos++;
        Serial.println(jogadores[i].esp + jogadores[i].pontos);
        acertou = true;

        // manda qual jogador acertou a resposta para o buzzer
        JsonDocument doc;
        String sms;

        doc["esp"] = jogadores[i].esp;
        doc["pontos"] = String(jogadores[i].pontos);

        serializeJson(doc, sms);
        client.publish(mqtt_topic_pub, sms.c_str());
        
        // mostra a resposta asssim que alguem acertar
        lcd.setCursor(indiceResposta - 2, 0);
        lcd.print("      ");
        lcd.setCursor(indiceResposta - 1, 0);
        lcd.print(respostaVerdadeira);

        // salva o tempo em que a resposta foi acertada
        tempoResposta = millis();

        break; // para sair assim que alguem acertar a respota
      }
    }

    // zerando todas as respostas, para evitar pontos infinitos
    jogadores[0].resposta = (numeroMaximo * numeroMaximo) + 1;
    jogadores[1].resposta = (numeroMaximo * numeroMaximo) + 1;

    // apos 3 segundos tira a resposta
    if (millis() - tempoResposta >= 3000 && acertou) {
      lcd.clear();
      respondido = true;
      gerado = false;
      acertou = false;
    }
  }


  // final do jogo, alguem venceu
  for (int i = 0; i < 2; i++) {
    if (jogadores[i].cor.equals(vencedorRecebido)) {
      mostrarVencedor();
      vencedorRecebido = "ninguem";
      vencedor = true;
      respondido = true;
      gerado = true;
      acertou = false;
    }
  }

  // volta ao inicio do jogo
  if (vencedor && millis() - tempoResposta >= 6000) {
    gerado = true;
    respondido = true;
    iniciar = true;
    vencedor = false;
    jogadores[0].iniciar = false;
    jogadores[1].iniciar = false;
    jogadores[0].pontos = 0;
    jogadores[1].pontos = 0;
    telaInicial();
    enviarFim();
  }

  enviarAnterior = enviar;
}

void conectaMqtt() {
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
  const char *msg = doc["msg"];
  const char *conec = doc["conectado"];
  const char *ini = doc["iniciar"];
  const char *resp = doc["resposta"];
  const char *venc = doc["vencedor"];

  // tratamento e atribuição dos valores recebidos
  for (int i = 0; i < qntdJogadores; i++) {
    if (jogadores[i].esp.equalsIgnoreCase(esp)) {
      jogadores[i].mensagem = msg;
      jogadores[i].resposta = atof(resp);
      if (strcmp(conec, "1") == 0) {
        jogadores[i].conectado = true;
        ninguem = false;
      }
      if (strcmp(ini, "1") == 0) jogadores[i].iniciar = true;
    }
  }

  if (venc != NULL) {

    if (strcasecmp(venc, "") != 0) {
      vencedorRecebido = String(venc);
    }
  }
}

// funcao que recebe numeros aletorios, gera uma expressao numerica e devolve a resposta
void gerarResposta(int n, int m) {

  Serial.println("gerando resposta");

  int alternativa = random(0, 3);
  float r = 0;
  String operacoes[] = {"+", "+", "*", "/"};

  lcd.clear();

  if (m == 0 && alternativa == 3) alternativa = random(0, 3);

  else {
    if (alternativa == 0) r = n + m;
    else if (alternativa == 1) r = n + m;
    else if (alternativa == 2) r = n * m;
    else if (alternativa == 3) r = n / m;
  }

  String expressao = String(n) + " " + operacoes[alternativa] + " " + String(m) + " = ??";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(expressao);
  Serial.println(expressao);

  respostaVerdadeira = r;
  indiceResposta = expressao.length() - 1;
}

void mostrarVencedor() {

  lcd.clear();
  lcd.setCursor(0, 0);

  lcd.print(vencedorRecebido);
  lcd.print(" VENCEU!");
  lcd.setCursor(0, 1);
  lcd.print("PARABENS!");
  
}

void telaInicial() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INICIAR?");
  lcd.setCursor(0, 1);
  lcd.print("Jogadores: 0");
}

void enviarIniciar() {
  String msg;
  JsonDocument doc;

  doc["fim"] = "0";
  serializeJson(doc, msg);

  client.publish(mqtt_topic_pub, msg.c_str());
  Serial.println(msg);
}

void enviarFim() {
  String msg;
  JsonDocument doc;

  doc["fim"] = "1";
  serializeJson(doc, msg);

  client.publish(mqtt_topic_pub, msg.c_str());
  Serial.println(msg);
}