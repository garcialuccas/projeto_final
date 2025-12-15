#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include "internet.h"

#define PINO_BUZZER 12

// Pinos de Controle de Velocidade (ENA/ENB)
#define PIN_PWM_DIREITA 2
#define PIN_PWM_ESQUERDA 4

// Pinos de Direção (IN1/IN2/IN3/IN4)
#define M_DIREITA_FRENTE 18
#define M_DIREITA_TRAS 19
#define M_ESQUERDA_TRAS 32
#define M_ESQUERDA_FRENTE 33 

#define LED_R 13
#define LED_B 5

const char *mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
const char *mqtt_client_id = "senai134_esp2_buzzer_match_game_OMNICODE"; // ID Único
const char *mqtt_topic_sub = "main_match_game_pub";
const char *mqtt_topic_pub = "main_match_game_sub";

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

String vencedorNome = "";
bool iniciar = false;
bool vencedor = false;
bool andarAzul = false;
bool andarVermelho = false;
bool modoRetorno = false;
unsigned long tempoAzul = 0;
unsigned long tempoVermelho = 0;
unsigned long tempoVoltar = 0;
unsigned long tempoInicioRetorno = 0;

int pontosAzul = 0;
int velocidadeAzul = 0;
int pontosVermelho = 0;
int velocidadeVermelho = 0;

const int FREQ_PWM = 1000;
const int CANAL_PWM_DIREITA = 0;
const int CANAL_PWM_ESQUERDA = 1;
const int RESOLUCAO_PWM = 8;

// --- Estrutura de Música ---
struct Nota {
  int frequencia;
  int duracao; 
  int pausa;   
};

// Notas musicais (Definições)
#define FREQ_MI1 660
#define FREQ_RE1 588
#define FREQ_DO1 524

const Nota musicaVitoria[] = {
   {FREQ_MI1, 150, 50}, {FREQ_MI1, 180, 50}, {FREQ_RE1, 1200, 150},
   {FREQ_RE1, 150, 50}, {FREQ_RE1, 180, 50}, {FREQ_DO1, 1200, 150}, 
   {FREQ_MI1, 150, 50}, {FREQ_MI1, 180, 50}, {FREQ_RE1, 1200, 130}, 
   {FREQ_RE1, 150, 50}, {FREQ_RE1, 180, 50}, {FREQ_DO1, 1200, 150}, 
};
const int TOTAL_NOTAS = sizeof(musicaVitoria) / sizeof(musicaVitoria[0]);

void pararMotores() {
    digitalWrite(M_DIREITA_FRENTE, LOW);
    digitalWrite(M_DIREITA_TRAS, LOW);
    digitalWrite(M_ESQUERDA_FRENTE, LOW);
    digitalWrite(M_ESQUERDA_TRAS, LOW);
    ledcWrite(CANAL_PWM_DIREITA, 0);
    ledcWrite(CANAL_PWM_ESQUERDA, 0);
}

void moverFrente() {
    // lógica Invertida (LOW/HIGH) para corrigir rotação oposta
    digitalWrite(M_ESQUERDA_FRENTE, LOW);
    digitalWrite(M_ESQUERDA_TRAS, HIGH);
    
    digitalWrite(M_DIREITA_FRENTE, LOW);
    digitalWrite(M_DIREITA_TRAS, HIGH);

    ledcWrite(CANAL_PWM_ESQUERDA, 255);
    ledcWrite(CANAL_PWM_DIREITA, 255);
}

void passoVermelho() {

    digitalWrite(M_DIREITA_FRENTE, LOW);
    digitalWrite(M_DIREITA_TRAS, HIGH);
    ledcWrite(CANAL_PWM_ESQUERDA, 255);

}

void passoAzul() {

    digitalWrite(M_ESQUERDA_FRENTE, LOW);
    digitalWrite(M_ESQUERDA_TRAS, HIGH);
    ledcWrite(CANAL_PWM_DIREITA, 255);

}

void moverTras() {
    // logica invertida para mover para trás
    digitalWrite(M_ESQUERDA_FRENTE, HIGH);
    digitalWrite(M_ESQUERDA_TRAS, LOW);
    
    digitalWrite(M_DIREITA_FRENTE, HIGH);
    digitalWrite(M_DIREITA_TRAS, LOW);

    ledcWrite(CANAL_PWM_ESQUERDA, 255);
    ledcWrite(CANAL_PWM_DIREITA, 255);
}

// funcao que toca a musica
void tocarMusicaBloqueante() {
    for (int i = 0; i < TOTAL_NOTAS; i++) {
        tone(PINO_BUZZER, musicaVitoria[i].frequencia, musicaVitoria[i].duracao);
        delay(musicaVitoria[i].duracao + musicaVitoria[i].pausa);
        noTone(PINO_BUZZER);
    }
}

void verificarSensor() {
    if (!iniciar || modoRetorno) return;

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) {
        int distancia = measure.RangeMilliMeter;
        
        // Debug
        Serial.printf("Dist: %d mm\n", distancia);

        bool detectou = false;
        
        // logica azul
        if (distancia > 30 && distancia < 60) {
            vencedorNome = "azul";
            detectou = true;
        }
        // logica vermelho
        else if (distancia > 70 && distancia < 150) {
            vencedorNome = "vermelho";
            detectou = true;
        }

        // envia o jogador que venceu
        if (detectou) {

            iniciar = false;

            JsonDocument doc;
            String sms;

            doc["vencedor"] = vencedorNome;
            serializeJson(doc, sms);
            client.publish(mqtt_topic_pub, sms.c_str());

            detectou = false;
        }
    }
}

void callbackMqtt(char *topic, byte *payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];
    
    Serial.print("MQTT Recebido: "); Serial.println(msg);

    JsonDocument doc;
    DeserializationError erro = deserializeJson(doc, msg);
    if (erro) return;

    // inicio e fim de jogo
    if (!doc["fim"].isNull()) {
        const char *fim = doc["fim"];
        if (strcmp(fim, "0") == 0) {
            // inicia o jogo
            Serial.println("COMANDO: INICIAR JOGO");
            pontosAzul = 0;
            velocidadeAzul = 0;
            pontosVermelho = 0;
            velocidadeVermelho = 0;
            iniciar = true;
            vencedor = false;
            modoRetorno = false;
        } 
        else if (strcmp(fim, "1") == 0) {
            // termina o jogo
            Serial.println("COMANDO: PARAR JOGO");
            iniciar = false;
            vencedor = true;
            pararMotores();
            moverTras();
            modoRetorno = true;
            tempoInicioRetorno = millis();
            tempoVoltar = millis();
        }
    }

    // mexendo os motores quando o jogador pontua
    if ((!doc["esp"].isNull() && !doc["pontos"].isNull()) && (iniciar && !modoRetorno)) {

        const char *esp = doc["esp"];
        
        if (strcmp(esp, "esp1") == 0) { // AZUL
            Serial.println("azul andando");
            andarAzul = true;
            tempoAzul = millis();
            passoAzul();
        }
        else if (strcmp(esp, "esp2") == 0) { // VERMELHO (?)
            Serial.println("vermelho andando");
            andarVermelho = true;
            tempoVermelho = millis();
            passoVermelho();
        }
    }
}

void reconectarMqtt() {
    if (!client.connected()) {
        Serial.print("Conectando MQTT...");
        String clientId = mqtt_client_id + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
            Serial.println("Conectado!");
            client.subscribe(mqtt_topic_sub);
        } else {
            Serial.print("Falha: "); Serial.print(client.state());
            delay(2000); // Delay curto na tentativa
        }
    }
}

void setup() {
    Serial.begin(115200);

    // Configuração de Pinos
    pinMode(PINO_BUZZER, OUTPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_B, OUTPUT);
    
    pinMode(M_DIREITA_FRENTE, OUTPUT);
    pinMode(M_DIREITA_TRAS, OUTPUT);
    pinMode(M_ESQUERDA_FRENTE, OUTPUT);
    pinMode(M_ESQUERDA_TRAS, OUTPUT);

    // Configuração PWM (ESP32 API v2.x)
    ledcSetup(CANAL_PWM_DIREITA, FREQ_PWM, RESOLUCAO_PWM);
    ledcSetup(CANAL_PWM_ESQUERDA, FREQ_PWM, RESOLUCAO_PWM);
    ledcAttachPin(PIN_PWM_DIREITA, CANAL_PWM_DIREITA);
    ledcAttachPin(PIN_PWM_ESQUERDA, CANAL_PWM_ESQUERDA);

    // Garantir estado inicial desligado
    pararMotores();

    // Inicialização Conexões
    conectaWiFi(); // Função do seu internet.h
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callbackMqtt);

    // Inicialização Sensor
    Wire.begin();
    if (!lox.begin()) {
        Serial.println(F("Falha ao iniciar VL53L0X"));
        while(1);
    }
    Serial.println(F("Sistema Pronto. Aguardando Start MQTT..."));
}

void loop() {
    // 1. Manter Conexões
    if (WiFi.status() != WL_CONNECTED) conectaWiFi();
    if (!client.connected()) reconectarMqtt();
    client.loop();

    // sempre verifica o sensor durante o loop
    verificarSensor();

    // para o motor durante o jogo, para o passo
    if (iniciar && !modoRetorno) {

        if (andarAzul && millis() - tempoAzul >= 100) {
            pararMotores();
            andarAzul = false;
        }

        if (andarVermelho && millis() - tempoVermelho >= 100) {
            pararMotores();
            andarVermelho = false;
        }

    }

    // fim de jogo
    if (modoRetorno) {
        unsigned long delta = millis() - tempoInicioRetorno;

        if (delta < 2000);
        
        else if (delta >= 2000 && delta < 2100) {
            pararMotores();
        }
        
        else if (delta >= 2100) {
            tocarMusicaBloqueante();
            modoRetorno = false;
            pararMotores();
            Serial.println("Estado: AGUARDANDO NOVO JOGO");
        }
    }
}