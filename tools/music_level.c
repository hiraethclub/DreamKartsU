/* DKU: loudness of an mp3, for tools/build_music.py.

     music_level song.mp3   ->   "<integrated LUFS> <sample peak dBFS>"

   Integrated loudness as ITU-R BS.1770-4 / EBU R128 (what upstream reads
   from ffmpeg's ebur128 filter): K-weighting (high shelf then high pass,
   coefficients from the analogue prototypes so any sample rate works),
   400 ms blocks with 75% overlap, an absolute gate at -70 LUFS and a
   relative gate 10 LU under the ungated mean. The peak is the sample peak,
   not ffmpeg's 4x oversampled true peak, so it can read up to a fraction
   of a dB low. */
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
} Biquad;

static double bq(Biquad *f, double x) {
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

static void k_weighting(double fs, Biquad *shelf, Biquad *hp) {
    double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
    double k = tan(M_PI * f0 / fs), vh = pow(10.0, g / 20.0), vb = pow(vh, 0.4996667741545416);
    double a0 = 1.0 + k / q + k * k;
    *shelf = (Biquad){(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                      2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0, 0, 0};
    f0 = 38.13547087602444;
    q = 0.5003270373238773;
    k = tan(M_PI * f0 / fs);
    a0 = 1.0 + k / q + k * k;
    *hp = (Biquad){1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0, 0, 0};
}

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: music_level song.mp3\n");
        return 2;
    }
    drmp3 mp3;
    if(!drmp3_init_file(&mp3, argv[1], NULL)) {
        fprintf(stderr, "music_level: cannot open %s\n", argv[1]);
        return 1;
    }
    unsigned ch = mp3.channels, fs = mp3.sampleRate;
    if(ch < 1 || ch > 2) return 1;
    Biquad shelf[2], hp[2];
    for(unsigned c = 0; c < ch; c++) k_weighting(fs, &shelf[c], &hp[c]);

    /* Mean square of each 100 ms quarter-block; blocks are four of them. */
    size_t quarter = fs / 10, nq = 0, capq = 1024, inq = 0;
    double *qs = malloc(capq * sizeof(double)), acc = 0, peak = 0;
    float buf[4096 * 2];
    drmp3_uint64 got;
    while((got = drmp3_read_pcm_frames_f32(&mp3, 4096, buf)) > 0) {
        for(drmp3_uint64 i = 0; i < got; i++) {
            for(unsigned c = 0; c < ch; c++) {
                double x = buf[i * ch + c];
                if(fabs(x) > peak) peak = fabs(x);
                double y = bq(&hp[c], bq(&shelf[c], x));
                acc += y * y;   /* L and R weigh 1.0; mono counts once */
            }
            if(++inq == quarter) {
                if(nq == capq) qs = realloc(qs, (capq *= 2) * sizeof(double));
                qs[nq++] = acc / (double)quarter;
                acc = 0;
                inq = 0;
            }
        }
    }
    drmp3_uninit(&mp3);

    double sum = 0;
    size_t n = 0;
    double *blk = malloc((nq + 1) * sizeof(double));
    size_t nb = 0;
    for(size_t i = 3; i < nq; i++) {
        double z = (qs[i - 3] + qs[i - 2] + qs[i - 1] + qs[i]) / 4.0;
        blk[nb++] = z;
        if(-0.691 + 10.0 * log10(z) > -70.0) {
            sum += z;
            n++;
        }
    }
    if(!n) {
        printf("-70.0 %.1f\n", 20.0 * log10(peak > 0 ? peak : 1e-9));
        return 0;
    }
    double rel = -0.691 + 10.0 * log10(sum / (double)n) - 10.0;
    sum = 0;
    n = 0;
    for(size_t i = 0; i < nb; i++) {
        double l = -0.691 + 10.0 * log10(blk[i]);
        if(l > -70.0 && l > rel) {
            sum += blk[i];
            n++;
        }
    }
    printf("%.2f %.2f\n", -0.691 + 10.0 * log10(sum / (double)n), 20.0 * log10(peak > 0 ? peak : 1e-9));
    free(blk);
    free(qs);
    return 0;
}
