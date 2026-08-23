/*
 * spectrogram.cpp  —  Forest Acoustic Classifier ESP32-S3
 *
 * STREAMING audio pipeline: reads audio HOP_LENGTH samples at a time so we
 * never need a full 80 KB PCM buffer. Only a 512-sample ring buffer is kept.
 *
 * Pipeline per frame:
 *   I2S (160 new samples) → ring buffer → Hann window → FFT (ESP-DSP)
 *   → magnitude → power mel filterbank → power_to_db(ref=max) → normalize → INT8
 *
 * log-mel stored as int16_t (× LOG_MEL_SCALE=100) to halve heap (32 KB vs 64 KB).
 *
 * Matches training exactly:
 *   librosa.melspectrogram(power=2) + power_to_db(ref=np.max) + z-score (std+1e-6)
 */

#include "spectrogram.h"
#include "spectrogram_params.h"
#include "audio_capture.h"
#include "hann_window.h"
#include "mel_filterbank.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_dsp.h"

static const char *TAG = "spectrogram";

#define INPUT_SCALE       0.053753f
#define INPUT_ZERO_POINT  17

// Scale factor for int16 log-mel storage: dB values (-80..0) × 100 → fit int16
#define LOG_MEL_SCALE     100

// ── Heap buffers ──────────────────────────────────────────────────────────────
static int16_t *s_ring_buf   = NULL;  // N_FFT  int16  — circular sample buffer
static float   *s_fft_buf    = NULL;  // N_FFT*2 float — interleaved re/im
static float   *s_mag_buf    = NULL;  // N_FFT_BINS float
static float   *s_mel_buf    = NULL;  // N_MELS float  — one frame power values
static int16_t *s_logmel_buf = NULL;  // N_MELS*N_FRAMES int16 — full spectrogram

esp_err_t spectrogram_init(void)
{
    s_ring_buf   = (int16_t *)malloc(N_FFT            * sizeof(int16_t));
    s_fft_buf    = (float   *)malloc(N_FFT * 2        * sizeof(float));
    s_mag_buf    = (float   *)malloc(N_FFT_BINS       * sizeof(float));
    s_mel_buf    = (float   *)malloc(N_MELS           * sizeof(float));
    s_logmel_buf = (int16_t *)malloc(N_MELS * N_FRAMES * sizeof(int16_t));

    if (!s_ring_buf || !s_fft_buf || !s_mag_buf || !s_mel_buf || !s_logmel_buf) {
        ESP_LOGE(TAG, "Heap allocation failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, N_FFT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-DSP FFT init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Spectrogram engine ready — streaming mode, %d KB heap",
             (int)((N_FFT*sizeof(int16_t) + N_FFT*2*sizeof(float) +
                    N_FFT_BINS*sizeof(float) + N_MELS*sizeof(float) +
                    N_MELS*N_FRAMES*sizeof(int16_t)) / 1024));
    return ESP_OK;
}

void spectrogram_deinit(void)
{
    free(s_ring_buf);    s_ring_buf   = NULL;
    free(s_fft_buf);     s_fft_buf    = NULL;
    free(s_mag_buf);     s_mag_buf    = NULL;
    free(s_mel_buf);     s_mel_buf    = NULL;
    free(s_logmel_buf);  s_logmel_buf = NULL;
}

/* ── FFT + mel for one frame ─────────────────────────────────────────────────
 * ring_read_pos: global index of the NEXT sample that will be written.
 * The ring buffer holds samples [ring_read_pos - N_FFT .. ring_read_pos - 1].
 * Frame f is centred at sample (f * HOP_LENGTH), window covers
 *   [f*HOP - N_FFT/2 .. f*HOP + N_FFT/2).
 * ─────────────────────────────────────────────────────────────────────────── */
static void process_frame(int frame_idx, int ring_read_pos)
{
    float *re_im = s_fft_buf;
    int centre   = frame_idx * HOP_LENGTH;
    int win_start = centre - N_FFT / 2;   // may reference negative (zero-padded)

    for (int i = 0; i < N_FFT; i++) {
        int global_idx = win_start + i;
        float sample = 0.0f;
        // valid sample only if within [0, N_SAMPLES) AND still in ring buffer
        if (global_idx >= 0 && global_idx < NUM_AUDIO_SAMPLES &&
            global_idx >= (ring_read_pos - N_FFT)) {
            sample = (float)s_ring_buf[global_idx % N_FFT];
        }
        re_im[2 * i]     = sample * hann_window[i];
        re_im[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_fc32(re_im, N_FFT);
    dsps_bit_rev_fc32(re_im, N_FFT);

    // Magnitude (for power spectrum we square later in mel step)
    for (int k = 0; k < N_FFT_BINS; k++) {
        float re = re_im[2 * k];
        float im = re_im[2 * k + 1];
        s_mag_buf[k] = sqrtf(re * re + im * im);
    }

    // Power mel: sum(mag² × filter)
    const float *fb = mel_filterbank;
    for (int m = 0; m < N_MELS; m++) {
        float energy = 0.0f;
        const float *row = fb + m * N_FFT_BINS;
        for (int k = 0; k < N_FFT_BINS; k++) {
            float mag = s_mag_buf[k];
            energy += (mag * mag) * row[k];
        }
        s_mel_buf[m] = energy;  // raw power, log applied after finding ref_max
    }
}

/* ── Main entry point ────────────────────────────────────────────────────────
 * Streaming: reads HOP_LENGTH samples at a time from I2S.
 * Pre-reads N_FFT/2 samples to implement librosa center=True padding.
 * ─────────────────────────────────────────────────────────────────────────── */
esp_err_t spectrogram_compute(int8_t *out_tensor)
{
    // Temp buffer for one hop of int32 I2S samples (on heap to avoid stack overflow)
    int32_t hop_raw[HOP_LENGTH];

    memset(s_ring_buf, 0, N_FFT * sizeof(int16_t));
    int ring_pos = 0;  // next write position (global index)

    // ── Pre-read N_FFT/2 real samples (implements centre=True left zero-pad) ──
    {
        int pre = N_FFT / 2;  // 256 samples
        // Read in chunks of HOP_LENGTH
        for (int done = 0; done < pre; ) {
            int chunk = (pre - done < HOP_LENGTH) ? (pre - done) : HOP_LENGTH;
            // Read `chunk` int32 samples from I2S
            size_t bytes_want = (size_t)chunk * sizeof(int32_t);
            size_t bytes_got  = 0;
            esp_err_t ret = i2s_channel_read(s_rx_chan, hop_raw, bytes_want,
                                             &bytes_got, pdMS_TO_TICKS(3000));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2S pre-read failed");
                return ret;
            }
            int got = (int)(bytes_got / sizeof(int32_t));
            for (int j = 0; j < got; j++) {
                s_ring_buf[ring_pos % N_FFT] = (int16_t)(hop_raw[j] >> 16);
                ring_pos++;
            }
            done += got;
        }
    }

    // ── Process N_FRAMES frames, reading HOP_LENGTH new samples before each ──
    const int total = N_MELS * N_FRAMES;
    float ref_max   = 1e-10f;

    for (int f = 0; f < N_FRAMES; f++) {

        // For frame > 0: read HOP_LENGTH more samples before processing
        if (f > 0) {
            size_t bytes_want = (size_t)HOP_LENGTH * sizeof(int32_t);
            size_t bytes_got  = 0;
            esp_err_t ret = i2s_channel_read(s_rx_chan, hop_raw, bytes_want,
                                             &bytes_got, pdMS_TO_TICKS(3000));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2S hop read failed at frame %d", f);
                return ret;
            }
            int got = (int)(bytes_got / sizeof(int32_t));
            for (int j = 0; j < got; j++) {
                s_ring_buf[ring_pos % N_FFT] = (int16_t)(hop_raw[j] >> 16);
                ring_pos++;
            }
        }

        // Compute FFT + mel powers for this frame
        process_frame(f, ring_pos);

        // Store raw power for each mel band; find ref_max along the way
        for (int m = 0; m < N_MELS; m++) {
            float p = s_mel_buf[m];
            if (p > ref_max) ref_max = p;
            // Temporarily store raw power as float bits in int16 scratch:
            // We'll overwrite with int16 dB after the loop.
            // Use the full float logmel (stored back as float via cast trick):
            // Simplest: store index f in the column, convert after.
            // Store raw power temporarily as float in out_tensor (scratch use).
            // Actually use a clean approach: store index-ordered into a float cast of out_tensor.
            // out_tensor is int8_t[N_MELS*N_FRAMES] = 16064 bytes.
            // We need N_MELS*N_FRAMES floats = 64256 bytes — doesn't fit.
            // Solution: accumulate frame-by-frame, store dB immediately using a
            // running ref_max after first pass. We'll do two passes:
            // Pass 1 stores raw power in s_logmel_buf (int16, scaled by 1e6 trick? No.)
            // Simpler: store raw float power in s_logmel_buf cast to int16 union.
            // CLEANEST: just store raw mel power in s_mel_buf per-frame, and keep
            // a separate float[N_MELS*N_FRAMES] — but that's 64 KB heap.
            // REAL SOLUTION: store dB(energy / 1.0) now, then shift globally after.
            // dB = 10*log10(energy) — we'll correct ref_max in second pass.

            // Store 10*log10(energy + eps) temporarily (will subtract ref_max later)
            float db_raw = 10.0f * log10f(p + 1e-10f);
            // pack as int16 scaled ×10 to preserve 0.1 dB precision: range ~[-1000, 500]
            int32_t packed = (int32_t)(db_raw * 10.0f);
            if (packed < -32768) packed = -32768;
            if (packed >  32767) packed =  32767;
            s_logmel_buf[m * N_FRAMES + f] = (int16_t)packed;
        }
    }

    // ── Second pass: subtract ref_max_db, clamp, z-score, quantize ──────────
    float ref_max_db = 10.0f * log10f(ref_max + 1e-10f);

    // Reconstruct float dB values, apply ref normalization, find stats
    float mean = 0.0f;
    for (int i = 0; i < total; i++) {
        float db = (float)s_logmel_buf[i] / 10.0f - ref_max_db;
        if (db < -80.0f) db = -80.0f;
        // Accumulate mean (we'll need another pass for std — or compute inline)
        // Store corrected dB back using same int16 trick (×10):
        int32_t packed = (int32_t)(db * 10.0f);
        if (packed < -32768) packed = -32768;
        if (packed >  32767) packed =  32767;
        s_logmel_buf[i] = (int16_t)packed;
        mean += db;
    }
    mean /= (float)total;

    float var = 0.0f;
    for (int i = 0; i < total; i++) {
        float db = (float)s_logmel_buf[i] / 10.0f;
        float d  = db - mean;
        var += d * d;
    }
    float std_dev = sqrtf(var / (float)total) + 1e-6f;

    // Quantize to INT8
    for (int i = 0; i < total; i++) {
        float db         = (float)s_logmel_buf[i] / 10.0f;
        float normalized = (db - mean) / std_dev;
        float q_f        = normalized / INPUT_SCALE + (float)INPUT_ZERO_POINT;
        int   q          = (int)roundf(q_f);
        if (q < -128) q = -128;
        if (q >  127) q =  127;
        out_tensor[i]    = (int8_t)q;
    }

    return ESP_OK;
}
