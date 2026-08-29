/*
 * classifier.cpp - TFLite Micro acoustic inference task.
 *
 * Adapted from the standalone forest_acoustic_classifier main.cpp: the model
 * load / interpreter setup and the capture->spectrogram->invoke loop now run
 * inside a dedicated FreeRTOS task, publishing the latest result for the LDSE
 * node role to transmit.
 */

#include "classifier.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_data.h"
#include "spectrogram_params.h"
#include "audio_capture.h"
#include "spectrogram.h"

static const char* TAG = "classifier";

// Label order must match the training label encoding
// (train_acoustic_model_5class.ipynb: axe, chainsaw, gunshot, handsaw, background).
const char* const ACOUSTIC_LABELS[ACOUSTIC_NUM_CLASSES] = {
    "Axe", "Chainsaw", "Gunshot", "Handsaw", "Background",
};

// Tensor arena — 260 KB (per project docs) for the tiny model on internal
// DRAM. NOTE: the previous 270 KB here overflowed the ESP32-S3's internal
// DRAM segment by ~3.2 KB once the rest of main/ldse/main.cpp's own BSS
// was linked in (dram0_0_seg overflow at link time); 260 KB is what
// AGENTS.md/CLAUDE.md document as the verified arena size.
#define TENSOR_ARENA_SIZE (260 * 1024)
static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

static SemaphoreHandle_t s_lock = nullptr;
static AcousticResult s_latest = {};
static bool s_have_result = false;
// Mic shares the sleep-gate MOSFET rail with the gas/DHT22 sensors (see
// AGENTS.md power topology). true at boot: LdseSleepGate::Begin() powers the
// rail on before classifier_start() runs.
static volatile bool s_mic_powered = true;

static void classifier_task(void* arg)
{
    (void)arg;

    const tflite::Model* model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        ESP_LOGE(TAG, "Model schema mismatch: got %d expected %d",
                 (int)model->version(), TFLITE_SCHEMA_VERSION);
        vTaskDelete(nullptr);
        return;
    }

    static tflite::MicroMutableOpResolver<12> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddMaxPool2D();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddReshape();
    resolver.AddMul();
    resolver.AddAdd();
    resolver.AddMean();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddLogistic();

    static tflite::MicroInterpreter interpreter(
        model, resolver, tensor_arena, TENSOR_ARENA_SIZE);
    if (interpreter.AllocateTensors() != kTfLiteOk)
    {
        ESP_LOGE(TAG, "AllocateTensors() failed");
        vTaskDelete(nullptr);
        return;
    }

    TfLiteTensor* input = interpreter.input(0);
    TfLiteTensor* output = interpreter.output(0);
    ESP_LOGI(TAG, "Arena used: %u / %u bytes",
             (unsigned)interpreter.arena_used_bytes(), (unsigned)TENSOR_ARENA_SIZE);
    ESP_LOGI(TAG, "Input quant: scale=%f zero_point=%d",
             input->params.scale, (int)input->params.zero_point);

    if (audio_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "audio_init() failed");
        vTaskDelete(nullptr);
        return;
    }
    if (spectrogram_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "spectrogram_init() failed");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Acoustic classifier running");
    int8_t* input_data = input->data.int8;

    for (;;)
    {
        if (!s_mic_powered)
        {
            // Sleep-gate MOSFET has the mic rail off: I2S DIN floats rather
            // than carrying real audio, so skip capture/inference instead of
            // classifying garbage samples as a possible threat.
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (spectrogram_compute(input_data, input->params.scale, input->params.zero_point) != ESP_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (interpreter.Invoke() != kTfLiteOk)
        {
            ESP_LOGE(TAG, "Invoke() failed");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        float scale = output->params.scale;
        int zp = output->params.zero_point;
        AcousticResult r = {};
        int best = 0;
        for (int i = 0; i < ACOUSTIC_NUM_CLASSES; i++)
        {
            r.confidence[i] = (output->data.int8[i] - zp) * scale;
            if (r.confidence[i] > r.confidence[best])
            {
                best = i;
            }
        }
        r.classIdx = (uint8_t)best;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_latest = r;
        s_have_result = true;
        xSemaphoreGive(s_lock);

        ESP_LOGI(TAG, "%s (%.2f)", ACOUSTIC_LABELS[best], r.confidence[best]);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

bool classifier_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == nullptr)
    {
        return false;
    }
    // 8 KB stack: inference uses the static arena, not the task stack.
    return xTaskCreate(classifier_task, "acoustic", 8192, nullptr, 4, nullptr) == pdPASS;
}

void classifier_set_mic_powered(bool powered)
{
    s_mic_powered = powered;
    if (!powered && s_lock != nullptr)
    {
        // Drop any cached result so a detection from before the rail went
        // down is never mistaken for a fresh one right after waking up.
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_have_result = false;
        xSemaphoreGive(s_lock);
    }
}

bool classifier_get_latest(AcousticResult* out)
{
    if (out == nullptr || s_lock == nullptr)
    {
        return false;
    }
    bool have;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    have = s_have_result;
    if (have)
    {
        *out = s_latest;
    }
    xSemaphoreGive(s_lock);
    return have;
}

bool classifier_is_threat(const AcousticResult* r)
{
    if (r == nullptr)
    {
        return false;
    }
    // Background (or an uncertain threat call below the confidence
    // threshold) is not an alert-worthy detection.
    return r->classIdx != ACOUSTIC_BACKGROUND_CLASS &&
           r->confidence[r->classIdx] >= ACOUSTIC_ALERT_THRESHOLD;
}
