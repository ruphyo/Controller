#include <esp_now.h>
#include <WiFi.h>
#include <ADS1115_WE.h> 
#include <Wire.h>

#define LED 2
#define I2C_ADDRESS 0x48


// Struktur für die zu sendenden Daten
typedef struct {
  uint32_t counter;
  float    value;
} sensor_data_t;

// Queue-Handle für die Kommunikation zwischen den Tasks
QueueHandle_t dataQueue;

// MAC-Adresse des Empfängers anpassen!
uint8_t peerAddress[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

// Constructor
ADS1115_WE adc = ADS1115_WE(I2C_ADDRESS);

// Forward-Deklarationen der Tasks
void taskReadAndProcess(void *pvParameters);
void taskSendESPNow(void *pvParameters);

// Optional: Callback für Sende-Status
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void setup() {
  //init I2C
  Wire.begin();
  //init serielle connection
  Serial.begin(115200);
  delay(1000);

  // WiFi im STA-Modus für ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  // ESP-NOW initialisieren
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    while (true) {         
        digitalWrite(LED, HIGH);
        delay(1000); 
        digitalWrite(LED,LOW);
        delay(1000); 
      }
  }

  //ADS1115 init
  if(!adc.init()){
    Serial.println("ADS1115 not connected!");
    digitalWrite(LED, HIGH);
    delay(2000); 
    digitalWrite(LED,LOW);
    delay(2000);
  }
  adc.setVoltageRange_mV(ADS1115_RANGE_6144); 

  // Callback registrieren (optional)
  esp_now_register_send_cb(onDataSent);

  // Peer konfigurieren
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;      // aktueller WiFi-Kanal
  peerInfo.encrypt = false;  // ggf. auf true und Key setzen

  if (!esp_now_is_peer_exist(peerAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      while (true) { 
        digitalWrite(LED, HIGH);
        delay(500); 
        digitalWrite(LED,LOW);
        delay(500);
      }
    }
  }

  // Queue erstellen (z. B. 10 Elemente)
  dataQueue = xQueueCreate(10, sizeof(sensor_data_t));
  if (dataQueue == NULL) {
    Serial.println("Error creating queue");
    while (true) { delay(1000); }
  }

  // Task 1: Daten einlesen und auswerten
  xTaskCreatePinnedToCore(
    taskReadAndProcess,
    "ReadProcessTask",
    4096,
    NULL,
    2,
    NULL,
    1       // Kern 1
  );

  // Task 2: Daten via ESP-NOW versenden
  xTaskCreatePinnedToCore(
    taskSendESPNow,
    "SendESPNowTask",
    4096,
    NULL,
    1,
    NULL,
    1       // ebenfalls Kern 1 (oder 0, je nach Wunsch)
  );
}

void loop() {
  // leer – FreeRTOS kümmert sich um die Tasks
}

// Task 1: Daten lesen und auswerten
void taskReadAndProcess(void *pvParameters) {
  sensor_data_t data;
  data.counter = 0;
  ADS1115_MUX Channel = ADS1115_COMP_0_GND;
  for (;;) {
    // Hier echte Sensorwerte einfügen, z. B. analogRead(...)
    
    float raw = readChannel(Channel);      // Beispiel-Pin
    mux(1);
    data.value = raw / 4095.0f;      // einfache „Auswertung“: Normierung
    data.counter++;

    // In Queue schreiben (blockiert max. 100 ms, wenn voll)
    if (xQueueSend(dataQueue, &data, pdMS_TO_TICKS(100)) == pdPASS) {
      Serial.print("Produced: #");
      Serial.print(data.counter);
      Serial.print(" value=");
      Serial.println(data.value, 3);
    } else {
      Serial.println("Queue full, data dropped");
    }

    //set Channel to next
    if(Channel == ADS1115_COMP_3_GND){
      //reset channel 0
      Channel = ADS1115_COMP_0_GND;
    } else {
      Channel = Channel + ADS1115_COMP_0_3;
    }
    
    // z. B. alle 200 ms neue Messung
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// Task 2: Daten aus Queue holen und senden
void taskSendESPNow(void *pvParameters) {
  sensor_data_t rxData;

  for (;;) {
    // Auf Daten aus der Queue warten (unbegrenzt)
    if (xQueueReceive(dataQueue, &rxData, portMAX_DELAY) == pdTRUE) {
      esp_err_t result = esp_now_send(
        peerAddress,
        (uint8_t *)&rxData,
        sizeof(rxData)
      );

      Serial.print("Sending #");
      Serial.print(rxData.counter);
      Serial.print(" value=");
      Serial.print(rxData.value, 3);
      Serial.print(" -> ");
      Serial.println(result == ESP_OK ? "OK" : "ERROR");
    }
  }
}

//read channel ADS1115
float readChannel(ADS1115_MUX channel) {
  float voltage = 0.0;
  adc.setCompareChannels(channel);
  adc.startSingleMeasurement();
  while(adc.isBusy()){}
  voltage = adc.getResult_V(); // alternative: getResult_mV for Millivolt
  return voltage;
}