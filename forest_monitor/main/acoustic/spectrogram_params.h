#pragma once

#define SAMPLE_RATE 16000
#define AUDIO_DURATION_SEC 2
#define NUM_AUDIO_SAMPLES 40000
#define N_FFT 512
#define N_FFT_HALF (N_FFT / 2)
#define N_FFT_BINS (N_FFT / 2 + 1)
#define N_MELS 64
#define HOP_LENGTH 160
#define N_FRAMES 251
#define LOG_OFFSET 1e-10f
