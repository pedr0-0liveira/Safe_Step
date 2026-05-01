#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h> // Garantir que as funções básicas do Arduino estejam disponíveis

// Definições de Pinos e Constantes

const int PINO_TRIG = 4;        // Pino TRIG do sensor ultrassônico (Saída)
const int PINO_ECHO = 2;        // Pino ECHO do sensor ultrassônico (Entrada)
# define MOT_PIN1 18            // Pino de controle do Motor de Vibração 1
# define MOT_PIN2 19            // Pino de controle do Motor de Vibração 2
const int PINO_buzzer = 23;     // Pino de controle do Buzzer (Alerta Sonoro)

// Definições de Canais PWM do ESP32 (0-15)
const int CH_MOTOR1 = 0;
const int CH_MOTOR2 = 1;
const int CH_BUZZER = 2;

// Configurações do PWM
const int PWM_FREQ_MOTORS = 20000; // Frequência para Motores: 20kHz
const int PWM_FREQ_BUZZER = 2000;  // Frequência para Buzzer: 2kHz
const int PWM_RESOLUTION = 8;      // Resolução: 8 bits (valores de 0 a 255)


// Variáveis de Medição e Controle de Distância

long duracao_us ;             // Duração do pulso ECHO em microssegundos
float distancia_us ;          // Distância final calculada e filtrada (em cm)
float media_distancia [5];    // Array para o filtro de Média Móvel (5 leituras)
float nova_distancia = 250;   // Armazena a última distância bruta lida
int indice = 0;               // Índice atual para o array de Média Móvel

// Variáveis para Controle de Tempo (Non-Blocking Delay)

unsigned long ultimoUltrassom = 0;   // Último momento em que o ultrassom foi lido
const int intervaloUltrassom = 1000; // Intervalo de 1000ms (1 segundo) para ler o ultrassom

unsigned long ultimoControlarAtuadores = 0; // Último momento que os atuadores foram checados
const int intervaloAtuadores = 500;        // Intervalo de 500ms (0.5 segundo) para atualizar atuadores

bool motoresAtivos = false ;   // Flag para verificar se os motores estão ligados

// --- Protótipos de Funções ---
void lerUltrassom ();
void controlarAtuadores ( float dist_us );


// --- Função map() do Arduino (para portabilidade) ---
long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// --- Funções Principais ---

void setup () {
    Serial . begin (115200) ;     // Aumentando a baud rate para melhor desempenho
    Serial.println("Inicializando ESP32...");
    
    // Configuração dos pinos do Ultrassom
    pinMode ( PINO_TRIG , OUTPUT ) ;
    pinMode ( PINO_ECHO , INPUT ) ;
    
    // Configuração dos pinos dos Motores (Atuadores Táteis)
    pinMode ( MOT_PIN1 , OUTPUT ) ;
    pinMode ( MOT_PIN2 , OUTPUT ) ;
    
    // === ATUALIZAÇÃO DO PWM: Usando ledcAttach (Substitui ledcSetup e ledcAttachPin) ===
    
    // Canal 0 (Motor 1): Associa o pino, frequência e resolução
    ledcAttach ( MOT_PIN1, PWM_FREQ_MOTORS, PWM_RESOLUTION ); 
    
    // Canal 1 (Motor 2): Associa o pino, frequência e resolução
    ledcAttach ( MOT_PIN2, PWM_FREQ_MOTORS, PWM_RESOLUTION ); 
    
    // Canal 2 (Buzzer): Associa o pino, frequência e resolução
    ledcAttach ( PINO_buzzer, PWM_FREQ_BUZZER, PWM_RESOLUTION );
    
    Serial.println("Configuracao PWM LEDC concluida.");
}


void loop () {
    // Verifica se é hora de ler o ultrassom (a cada 1000ms)
    if ( millis () - ultimoUltrassom >= intervaloUltrassom ) {
        lerUltrassom () ;             // Chama a função de leitura
        ultimoUltrassom = millis () ;   // Atualiza o tempo da última leitura
    }
    
    // Verifica se é hora de controlar os atuadores (a cada 500ms)
    if ( millis () - ultimoControlarAtuadores >= intervaloAtuadores ) {
        controlarAtuadores ( distancia_us ) ;   // Passa a distância filtrada para o controle
        ultimoControlarAtuadores = millis () ;  // Atualiza o tempo do último controle
    }
}

// --- Função de Leitura do Sensor Ultrassônico ---

void lerUltrassom () {
    // Sequência padrão para gerar o pulso TRIG
    digitalWrite ( PINO_TRIG , LOW ) ;
    delayMicroseconds (2) ;
    digitalWrite ( PINO_TRIG , HIGH ) ;
    delayMicroseconds (10) ;
    digitalWrite ( PINO_TRIG , LOW ) ;
    
    // Mede a duração do pulso ECHO (tempo de ida e volta do som)
    // Usamos pulseIn com um timeout de 60ms (suficiente para até 10 metros)
    duracao_us = pulseIn ( PINO_ECHO , HIGH , 60000) ; 
    
    // Lógica para lidar com timeout ou distância muito grande
    if ( duracao_us == 0 || duracao_us > 17493) { // 17493 us = aproximadamente 300cm de ida e volta
        nova_distancia = 400.0; // Define 400cm como distância máxima (objeto fora do alcance)
    } else {
        // Cálculo da distância em cm: (duração * velocidade do som) / 2
        // 0.0343 cm/us é a metade da velocidade do som em cm/us
        nova_distancia = ( (float)duracao_us * 0.0343) / 2.0;
    }
    
    // Filtro de Média Móvel
    media_distancia [ indice ] = nova_distancia ; // Armazena a nova leitura
    indice = ( indice + 1) % 5;                 // Move para o próximo índice (circula 0 a 4)
    
    // Calcula a média das 5 últimas leituras
    distancia_us = 0.0;
    for (int i = 0; i < 5; i ++) {
        distancia_us += media_distancia [ i ];
    }
    distancia_us /= 5.0;
    
    // Impressão da distância para debug
    Serial . print (" Ultrassom : ") ;
    Serial . print ( distancia_us ) ;
    Serial . println (" cm") ;
}

// Apêndice A. Código-fonte 54

// --- Função de Controle dos Motores e Buzzer ---

void controlarAtuadores ( float dist_us ) {
    
    // Alerta Sonoro (Buzzer) para distância CRÍTICA (<= 150cm)
    if ( dist_us > 0 && dist_us <= 150.0) {
        // Usa o canal CH_BUZZER (2)
        ledcWrite (CH_BUZZER , 255) ;    // Liga o Buzzer na intensidade máxima
        delay (500) ;                   // Mantém ligado por 500ms (O uso de delay aqui vai pausar o loop, mas é comum em alarmes)
        ledcWrite (CH_BUZZER , 0) ;     // Desliga o Buzzer
    } else {
        ledcWrite (CH_BUZZER , 0); // Garante que o buzzer esteja desligado fora da zona crítica
    }
    
    // Alerta Tátil (Motores) PROPORCIONAL para distância de 300cm a 150cm
    if ( dist_us <= 300.0 && dist_us > 150.0) {
        // Cálculo da intensidade: Mapeia [300, 150] cm para [150, 255] de vibração
        // Note: A função map usa 'long', então fazemos o cast para float depois
        int intensidade = map ( (long)dist_us , 300L , 150L , 150L , 255L) ;
        intensidade = constrain ( intensidade , 150 , 255) ; // Garante que a intensidade fique entre 150 e 255
        
        // Ativa os motores com a intensidade calculada (Canais 0 e 1)
        ledcWrite (CH_MOTOR1 , intensidade ) ;
        ledcWrite (CH_MOTOR2 , intensidade ) ;
        motoresAtivos = true ; // Marca que os motores estão ligados
        
        // Impressão da intensidade para debug
        Serial . print (" Intensidade dos motores : ") ;
        Serial . println ( intensidade ) ;
    } else {
        // Se a distância for > 300cm ou <= 150cm (Buzzer ativo), desliga os motores
        if ( motoresAtivos ) {
            ledcWrite (CH_MOTOR1 , 0) ;
            ledcWrite (CH_MOTOR2 , 0) ;
            motoresAtivos = false ;
            Serial.println("Motores Desligados.");
        }
    }
}