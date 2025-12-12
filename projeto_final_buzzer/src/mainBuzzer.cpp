#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include "internet.h" // Certifique-se que este arquivo existe e tem as credenciais

// --- Configurações de Hardware ---
#define PINO_BUZZER 12

// Pinos de Controle de Velocidade (ENA/ENB)
#define PIN_PWM_DIREITA 2
#define PIN_PWM_ESQUERDA 4

// Pinos de Direção (IN1/IN2/IN3/IN4)
#define M_DIREITA_FRENTE 18
#define M_DIREITA_TRAS 19
#define M_ESQUERDA_TRAS 33
#define M_ESQUERDA_FRENTE 32 

#define LED_R 13
#define LED_B 5

// --- Configurações MQTT ---
const char *mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
const char *mqtt_client_id = "senai134_esp2_buzzer_match_game_OMNICODE"; // ID Único
const char *mqtt_topic_sub = "main_match_game_pub";
const char *mqtt_topic_pub = "main_match_game_sub";

// --- Objetos Globais ---
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

// --- Variáveis de Estado ---
enum EstadoJogo {
    AGUARDANDO,
    CORRIDA,
    RETORNO,
    VITORIA_DETECTADA
};

EstadoJogo estadoAtual = AGUARDANDO;
String vencedorNome = "";

int pontosAzul = 0;
int velocidadeAzul = 0;
int pontosVermelho = 0;
int velocidadeVermelho = 0;

// --- Configuração PWM ---
const int FREQ_PWM = 5000; // Reduzido para 5kHz para melhor torque no L298N
const int CANAL_PWM_DIREITA = 0; // Canais ajustados
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

// ===================================================================================
// --- IMPLEMENTAÇÃO ---
// ===================================================================================

void pararMotores() {
    digitalWrite(M_DIREITA_FRENTE, LOW);
    digitalWrite(M_DIREITA_TRAS, LOW);
    digitalWrite(M_ESQUERDA_FRENTE, LOW);
    digitalWrite(M_ESQUERDA_TRAS, LOW);
    ledcWrite(CANAL_PWM_DIREITA, 0);
    ledcWrite(CANAL_PWM_ESQUERDA, 0);
}

void moverFrente(int velEsq, int velDir) {
    // CORREÇÃO DE DIREÇÃO: Lógica Invertida (LOW/HIGH) para corrigir rotação oposta
    digitalWrite(M_ESQUERDA_FRENTE, LOW);
    digitalWrite(M_ESQUERDA_TRAS, HIGH);
    
    digitalWrite(M_DIREITA_FRENTE, LOW);
    digitalWrite(M_DIREITA_TRAS, HIGH);

    ledcWrite(CANAL_PWM_ESQUERDA, velEsq);
    ledcWrite(CANAL_PWM_DIREITA, velDir);
}

void moverTras() {
    // CORREÇÃO DE DIREÇÃO: Lógica Invertida para mover para trás
    digitalWrite(M_ESQUERDA_FRENTE, HIGH);
    digitalWrite(M_ESQUERDA_TRAS, LOW);
    
    digitalWrite(M_DIREITA_FRENTE, HIGH);
    digitalWrite(M_DIREITA_TRAS, LOW);

    ledcWrite(CANAL_PWM_ESQUERDA, 255);
    ledcWrite(CANAL_PWM_DIREITA, 255);
}

void tocarMusicaBloqueante() {
    // Nota: Em sistemas críticos, usaríamos millis() e interrupções.
    // Mantido simples aqui, mas ciente que bloqueia o MQTT momentaneamente.
    for (int i = 0; i < TOTAL_NOTAS; i++) {
        tone(PINO_BUZZER, musicaVitoria[i].frequencia, musicaVitoria[i].duracao);
        delay(musicaVitoria[i].duracao + musicaVitoria[i].pausa);
        noTone(PINO_BUZZER);
    }
}

// Filtro para evitar leituras falsas (Ghost Sensing)
int contadorValidacaoSensor = 0;
const int LEITURAS_PARA_VALIDAR = 3; 

void verificarSensor() {
    if (estadoAtual != CORRIDA) return;

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) {
        int distancia = measure.RangeMilliMeter;
        
        // Debug
        // Serial.printf("Dist: %d mm\n", distancia);

        bool detectou = false;
        
        // Lógica Azul
        if (distancia > 30 && distancia < 60) {
            vencedorNome = "azul";
            detectou = true;
        }
        // Lógica Vermelho
        else if (distancia > 70 && distancia < 150) {
            vencedorNome = "vermelho"; // Assumi vermelho baseado no código anterior
            detectou = true;
        }

        if (detectou) {
            contadorValidacaoSensor++;
            if (contadorValidacaoSensor >= LEITURAS_PARA_VALIDAR) {
                estadoAtual = VITORIA_DETECTADA;
                Serial.println("Vencedor Confirmado: " + vencedorNome);
            }
        } else {
            contadorValidacaoSensor = 0;
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

    // 1. Controle de Jogo (Start/Stop)
    // FIX: containsKey deprecated in ArduinoJson v7. Substituido por !doc["key"].isNull()
    if (!doc["fim"].isNull()) {
        const char *fim = doc["fim"];
        if (strcmp(fim, "0") == 0) {
            // INICIAR JOGO
            Serial.println("COMANDO: INICIAR JOGO");
            pontosAzul = 0;
            velocidadeAzul = 0;
            pontosVermelho = 0;
            velocidadeVermelho = 0;
            estadoAtual = CORRIDA;
            contadorValidacaoSensor = 0;
        } 
        else if (strcmp(fim, "1") == 0) {
            // PARAR JOGO
            Serial.println("COMANDO: PARAR JOGO");
            estadoAtual = AGUARDANDO;
            pararMotores();
        }
    }

    // 2. Atualização de Pontos (apenas se estiver em jogo ou aguardando)
    // FIX: containsKey deprecated. Substituido por checagem de nulidade.
    if (!doc["esp"].isNull() && !doc["pontos"].isNull()) {
        const char *esp = doc["esp"];
        
        if (strcmp(esp, "esp1") == 0) { // AZUL
            pontosAzul++;
            if(pontosAzul > 10) pontosAzul = 10;
            velocidadeAzul = map(pontosAzul, 0, 10, 60, 255); // Minimo 60 para vencer inércia
            Serial.printf("Azul Vel: %d\n", velocidadeAzul);
        }
        else if (strcmp(esp, "esp2") == 0) { // VERMELHO (?)
            pontosVermelho++;
            if(pontosVermelho > 10) pontosVermelho = 10;
            velocidadeVermelho = map(pontosVermelho, 0, 10, 60, 255);
            Serial.printf("Vermelho Vel: %d\n", velocidadeVermelho);
        }
    }
}

void reconectarMqtt() {
    if (!client.connected()) {
        Serial.print("Conectando MQTT...");
        // Usa ID único + Random para evitar desconexão por conflito de ID
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

    // Garantir estado inicial desligado
    pararMotores();

    // Configuração PWM (ESP32 API v2.x)
    ledcSetup(CANAL_PWM_DIREITA, FREQ_PWM, RESOLUCAO_PWM);
    ledcSetup(CANAL_PWM_ESQUERDA, FREQ_PWM, RESOLUCAO_PWM);
    ledcAttachPin(PIN_PWM_DIREITA, CANAL_PWM_DIREITA);
    ledcAttachPin(PIN_PWM_ESQUERDA, CANAL_PWM_ESQUERDA);

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

    // 2. Máquina de Estados
    switch (estadoAtual) {
        case AGUARDANDO:
            pararMotores();
            // Apenas espera comando MQTT
            break;

        case CORRIDA:
            // Atualiza velocidade baseada nos pontos recebidos
            // ATENÇÃO: Se velocidade for 0, motores parados.
            // Se inverteu a fiação (Frente/Tras), troque a função aqui.
            moverFrente(velocidadeAzul, velocidadeVermelho); 
            
            verificarSensor();
            break;

        case VITORIA_DETECTADA:
            // Envia MQTT
            {
                JsonDocument doc;
                String msg;
                doc["vencedor"] = vencedorNome;
                serializeJson(doc, msg);
                client.publish(mqtt_topic_pub, msg.c_str());
            }
            
            estadoAtual = RETORNO;
            break;

        case RETORNO:
            // Recua o carrinho
            moverTras();
            tocarMusicaBloqueante(); // Toca a música (bloqueante)
            
            // Após música, reseta
            pararMotores();
            estadoAtual = AGUARDANDO;
            
            // Opcional: Resetar pontos
            pontosAzul = 0; pontosVermelho = 0;
            velocidadeAzul = 0; velocidadeVermelho = 0;
            break;
    }
}