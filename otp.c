#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
    unsigned long initial, multiplier, increment, modulus;
} prng_config;

typedef struct {
    unsigned char *input_data;
    unsigned char *random_pad;
    unsigned char *result_data;
    size_t length;
    prng_config config;
    int thread_count;
    pthread_barrier_t sync_point;
} shared_data;

typedef struct {
    int thread_id;
    shared_data *shared;
    size_t from;
    size_t to;
} thread_params;

void *generate_pad(void *arg) {
    shared_data *data = (shared_data *)arg;
    data->random_pad = malloc(data->length);
    unsigned long value = data->config.initial;

    for (size_t i = 0; i < data->length; ++i) {
        value = (data->config.multiplier * value + data->config.increment) % data->config.modulus;
        data->random_pad[i] = (unsigned char)(value & 0xFF);
    }
    return NULL;
}

void *xor_worker(void *arg) {
    thread_params *params = (thread_params *)arg;
    shared_data *data = params->shared;

    for (size_t i = params->from; i < params->to; ++i) {
        data->result_data[i] = data->input_data[i] ^ data->random_pad[i];
    }

    pthread_barrier_wait(&data->sync_point);
    free(params);
    return NULL;
}

int detect_cores() {
    int cores = 1;
#ifdef _SC_NPROCESSORS_ONLN
    cores = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    cores = sysinfo.dwNumberOfProcessors;
#endif
    return (cores < 1) ? 1 : cores;
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    char *src = NULL, *dst = NULL;
    prng_config gen_cfg = {0};
    int opt;
    clock_t t_start, t_end;

    while ((opt = getopt(argc, argv, "i:o:x:a:c:m:")) != -1) {
        switch (opt) {
            case 'i': src = optarg; break;
            case 'o': dst = optarg; break;
            case 'x': gen_cfg.initial = strtoul(optarg, NULL, 10); break;
            case 'a': gen_cfg.multiplier = strtoul(optarg, NULL, 10); break;
            case 'c': gen_cfg.increment = strtoul(optarg, NULL, 10); break;
            case 'm': gen_cfg.modulus = strtoul(optarg, NULL, 10); break;
            default:
                fprintf(stderr, "Пример: %s -i вход -o выход -x seed -a A -c C -m M\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (!src || !dst || gen_cfg.modulus == 0) {
        fprintf(stderr, "Ошибка: недостаточно аргументов.\n");
        return EXIT_FAILURE;
    }

    FILE *fin = fopen(src, "rb");
    if (!fin) {
        perror("Ошибка открытия входного файла");
        return EXIT_FAILURE;
    }

    fseek(fin, 0, SEEK_END);
    size_t fsize = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    unsigned char *buffer = malloc(fsize);
    if (!buffer) {
        perror("Ошибка выделения памяти");
        fclose(fin);
        return EXIT_FAILURE;
    }

    fread(buffer, 1, fsize, fin);
    fclose(fin);

    shared_data ctx;
    ctx.input_data = buffer;
    ctx.length = fsize;
    ctx.config = gen_cfg;
    ctx.result_data = malloc(fsize);

    if (!ctx.result_data) {
        fprintf(stderr, "Ошибка: память под результат не выделена.\n");
        free(buffer);
        return EXIT_FAILURE;
    }

    t_start = clock();

    pthread_t prng_thread;
    pthread_create(&prng_thread, NULL, generate_pad, &ctx);
    pthread_join(prng_thread, NULL);

    ctx.thread_count = detect_cores();
    pthread_barrier_init(&ctx.sync_point, NULL, ctx.thread_count + 1);
    pthread_t *workers = malloc(ctx.thread_count * sizeof(pthread_t));

    size_t step = fsize / ctx.thread_count;
    size_t remainder = fsize % ctx.thread_count;

    for (int i = 0; i < ctx.thread_count; ++i) {
        thread_params *params = malloc(sizeof(thread_params));
        params->thread_id = i;
        params->shared = &ctx;
        params->from = i * step;
        params->to = (i == ctx.thread_count - 1) ? (i + 1) * step + remainder : (i + 1) * step;
        pthread_create(&workers[i], NULL, xor_worker, params);
    }

    pthread_barrier_wait(&ctx.sync_point);

    FILE *fout = fopen(dst, "wb");
    if (!fout) {
        perror("Ошибка открытия выходного файла");
        free(buffer);
        free(ctx.result_data);
        free(ctx.random_pad);
        free(workers);
        return EXIT_FAILURE;
    }

    fwrite(ctx.result_data, 1, fsize, fout);
    fclose(fout);

    for (int i = 0; i < ctx.thread_count; ++i) {
        pthread_join(workers[i], NULL);
    }

    t_end = clock();
    double duration = (double)(t_end - t_start) / CLOCKS_PER_SEC;

    free(workers);
    pthread_barrier_destroy(&ctx.sync_point);
    free(ctx.input_data);
    free(ctx.random_pad);
    free(ctx.result_data);

    printf("Processing completed in %.3f seconds\n", duration);
    printf("Source-file: %s (%zu byte)\n", src, fsize);
    printf("Result-file: %s (%zu byte)\n", dst, fsize);

    return 0;
}
