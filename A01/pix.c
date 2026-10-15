#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Valores em centavos. Cada usuario transfere uma unica vez. */
#define VALOR 100
#define SALDO_INICIAL 1000
#define MAX_USUARIOS 100000
#define MAX_THREADS 256
int n, tamanho, protegido, tempo_ms, janela_ms, silencioso;
// Vetores fixos: nao precisamos de malloc, calloc ou free.
int contas[MAX_USUARIOS];
int estado[MAX_USUARIOS];
int ocupacao[MAX_THREADS];
int destino, proximo, concluidos;
pthread_t workers[MAX_THREADS];
int ids[MAX_THREADS];
/* 0 = aguardando; 1 = executando; 2 = concluida. */
pthread_mutex_t fila = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t transacao = PTHREAD_MUTEX_INITIALIZER;
/* Acesso individual evita comportamento indefinido de C. Nao torna
   leitura + calculo + escrita uma transacao indivisivel. */
pthread_mutex_t acesso = PTHREAD_MUTEX_INITIALIZER;
sem_t inicio;
// Cada Ctrl+C registra um pedido de consulta neste semaforo.
sem_t consultas;

void pedir_debug(int sinal) {
    int erro_anterior = errno; // Nao altera o erro da operacao interrompida.
    if (sinal == SIGINT) {
        // sem_post pode ser usado em tratadores de sinais POSIX.
        // Nao usamos printf nem mutex dentro do tratador.
        sem_post(&consultas);
    }
    errno = erro_anterior;
}

void pausa(int ms) {
    if (ms == 0) return;
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) == -1) {
        if (errno != EINTR) { perror("nanosleep"); exit(1); }
    }
}
void transferir(int usuario) {
    /* Apenas esta exclusao protege a operacao completa. */
    if (protegido) {
        pthread_mutex_lock(&transacao);
    }
    pthread_mutex_lock(&acesso);
    int saldo_origem = contas[usuario];
    int saldo_destino = destino;
    pthread_mutex_unlock(&acesso);

    /* Janela didatica explicita: permite intercalacao entre ler e escrever.
       Nao representa latencia medida do PIX real. */
    pausa(janela_ms);
    if (saldo_origem >= VALOR) {
        pthread_mutex_lock(&acesso);
        contas[usuario] = saldo_origem - VALOR;
        destino = saldo_destino + VALOR;
        pthread_mutex_unlock(&acesso);
    }
    if (protegido) {
        pthread_mutex_unlock(&transacao);
    }
}
// pthread_create exige funcao que recebe void * e retorna void *.
// O argumento recebido aqui e o endereco do ID do trabalhador.
void *trabalhador(void *arg) {
    int *numero_worker = arg;
    int id = *numero_worker;
    while (sem_wait(&inicio) == -1) {
        if (errno != EINTR) { perror("sem_wait"); exit(1); }
    }
    while (1) {
        pthread_mutex_lock(&fila);
        if (proximo == n) {
            pthread_mutex_unlock(&fila);
            break;
        }
        /* O vetor de requisicoes e uma FIFO pre-carregada: retirada pela cabeca.
           Todas chegam em t=0; ID resolve empates de chegada. */
        int req = proximo;
        proximo++;
        estado[req] = 1;
        ocupacao[id] = req;
        if (!silencioso)
            printf("DESPACHO req=%d worker=%d ordem=%d\n",
                   req+1, id+1, proximo);
        pthread_mutex_unlock(&fila);

        pausa(tempo_ms); /* Processamento fora da secao critica bancaria. */
        transferir(req);

        pthread_mutex_lock(&fila);
        estado[req] = 2;
        ocupacao[id] = -1;
        concluidos++;
        if (!silencioso) printf("FIM req=%d worker=%d\n", req+1, id+1);
        pthread_mutex_unlock(&fila);
    }
    return NULL;
}
// Chamada pela thread principal, fora do tratador de sinal.
void mostrar_fila(void) {
    pthread_mutex_lock(&fila);
    printf("\nDEBUG concluidas=%d/%d\nFIFO aguardando:", concluidos, n);
    for (int i = proximo; i < n; i++) printf(" %d", i+1);
    if (proximo == n) printf(" vazia");
    printf("\nPool:");
    for (int i = 0; i < tamanho; i++) {
        if (ocupacao[i] == -1) printf(" [worker %d: livre]", i+1);
        else printf(" [worker %d: req %d]", i+1, ocupacao[i]+1);
    }
    printf("\nConcluidas:");
    for (int i = 0; i < n; i++) {
        if (estado[i] == 2) printf(" %d", i+1);
    }
    printf("\nFIM DEBUG\n\n");
    pthread_mutex_unlock(&fila);
}
// Le e valida os numeros digitados no terminal.
int numero(const char *s, int minimo, int maximo) {
    char *fim;
    errno = 0;
    long v = strtol(s, &fim, 10);
    if (errno || !*s || *fim || v < minimo || v > maximo) {
        fprintf(stderr, "Numero invalido: %s (intervalo %d..%d)\n", s, minimo, maximo);
        exit(1);
    }
    return (int)v;
}
int main(int argc, char *argv[]) {
    if (argc < 6 || argc > 7 ||
        (strcmp(argv[1], "sem") && strcmp(argv[1], "com")) ||
        (argc == 7 && strcmp(argv[6], "quiet"))) {
        fprintf(stderr, "Uso: %s sem|com N pool tempo_ms janela_ms [quiet]\n", argv[0]);
        return 1;
    }
    protegido = strcmp(argv[1], "com") == 0;
    n = numero(argv[2], 1, MAX_USUARIOS);
    tamanho = numero(argv[3], 1, MAX_THREADS);
    tempo_ms = numero(argv[4], 0, 60000);
    janela_ms = numero(argv[5], 0, 60000);
    silencioso = argc == 7;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i=0; i<n; i++) contas[i] = SALDO_INICIAL;
    for (int i=0; i<tamanho; i++) ocupacao[i] = -1;
    if (sem_init(&inicio, 0, 0) == -1 || sem_init(&consultas, 0, 0) == -1) {
        perror("sem_init");
        return 1;
    }
    // signal() associa Ctrl+C (SIGINT) a pedir_debug, como na aula 4.
    if (signal(SIGINT, pedir_debug) == SIG_ERR) {
        perror("signal");
        return 1;
    }
    for (int i = 0; i < tamanho; i++) {
        ids[i] = i;
        if (pthread_create(&workers[i], NULL, trabalhador, &ids[i]) != 0) {
            fprintf(stderr, "Nao foi possivel criar o trabalhador %d.\n", i+1);
            exit(1);
        }
    }
    if (!silencioso) printf("N=%d pool=%d modo=%s. Ctrl+C mostra a fila; Ctrl+\\ encerra.\n", n, tamanho, argv[1]);
    for (int i=0; i<tamanho; i++) {
        if (sem_post(&inicio)) { perror("sem_post"); return 1; }
    }
    // A principal atende pedidos de debug enquanto o pool trabalha.
    while (1) {
        // Retira pedidos sem bloquear quando o semaforo esta em zero.
        while (sem_trywait(&consultas) == 0) {
            mostrar_fila();
        }
        if (errno != EAGAIN && errno != EINTR) {
            perror("sem_trywait");
            exit(1);
        }
        pthread_mutex_lock(&fila);
        int terminou = concluidos == n;
        pthread_mutex_unlock(&fila);
        if (terminou) break;
        pausa(10); // Evita espera ocupada e permite consultas durante a execucao.
    }
    for (int i = 0; i < tamanho; i++) {
        if (pthread_join(workers[i], NULL) != 0) {
            fprintf(stderr, "Erro ao aguardar trabalhador.\n");
            exit(1);
        }
    }
    int total = destino;
    int origens_ok = 1;
    for (int i=0; i<n; i++) {
        total += contas[i];
        if (contas[i] != SALDO_INICIAL-VALOR) origens_ok = 0;
    }
    int esperado = n * VALOR;
    int ok = destino == esperado && total == n*SALDO_INICIAL && origens_ok;
    printf("RESULTADO modo=%s N=%d pool=%d esperado=%d obtido=%d perdido=%d total=%d total_inicial=%d origens_ok=%d ok=%d\n",
           argv[1], n, tamanho, esperado, destino, esperado-destino, total,
           n*SALDO_INICIAL, origens_ok, ok);
    // consultas vive ate o processo terminar: o tratador pode usa-lo ate la.
    sem_destroy(&inicio);
    pthread_mutex_destroy(&fila);
    pthread_mutex_destroy(&transacao);
    pthread_mutex_destroy(&acesso);
    return 0; /* Inconsistencia no modo didatico e resultado, nao erro de execucao. */
}
