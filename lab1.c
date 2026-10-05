/*
 * Лабораторная работа №1. Базовые системные вызовы сокетов и адресация.
 * Уровень: продвинутый. Вариант 11.
 * Доп. требование: проверить, что при SOCK_STREAM и protocol = 0
 *                  ядро выбирает протокол TCP.
 *
 * Использование: ./lab1 <domain> <type> <ip> <port> [--bind]
 *   domain : AF_INET
 *   type   : SOCK_STREAM | SOCK_DGRAM
 *   ip     : IPv4-адрес в текстовом виде, например 127.0.0.1
 *   port   : 0..65535
 *   --bind : выполнить bind() сокета на указанные ip:port
 *
 * Сборка: gcc -std=c11 -Wall -Wextra -o lab1 lab1.c
 */

#define _GNU_SOURCE /* нужен для dirfd(), readlink(), SO_PROTOCOL при -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ---------- Вспомогательные функции ---------- */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Использование: %s <domain> <type> <ip> <port> [--bind]\n"
            "  domain : AF_INET\n"
            "  type   : SOCK_STREAM | SOCK_DGRAM\n"
            "  ip     : IPv4-адрес, например 127.0.0.1\n"
            "  port   : число от 0 до 65535\n"
            "  --bind : привязать сокет к ip:port\n",
            prog);
}

/* Разбор порта: 0 - успех, -1 - не число, -2 - вне диапазона 0..65535 */
static int parse_port(const char *s, uint16_t *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0)
        return -1;
    if (v < 0 || v > 65535)
        return -2;
    *out = (uint16_t)v;
    return 0;
}

static const char *proto_name(int proto)
{
    switch (proto)
    {
    case IPPROTO_TCP:
        return "TCP";
    case IPPROTO_UDP:
        return "UDP";
    default:
        return "неизвестный";
    }
}

/* Печать таблицы открытых дескрипторов процесса через /proc/self/fd */
static void print_fds(const char *label)
{
    DIR *d = opendir("/proc/self/fd");
    if (d == NULL)
    {
        perror("opendir(/proc/self/fd)");
        return;
    }
    int self_fd = dirfd(d); /* дескриптор самого opendir() не показываем */

    printf("--- Открытые дескрипторы (%s) ---\n", label);
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        int fd = atoi(e->d_name);
        if (fd == self_fd)
            continue;

        char path[300];
        char target[256];
        snprintf(path, sizeof(path), "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n < 0)
            continue;
        target[n] = '\0';
        printf("  fd %-3d -> %s\n", fd, target);
    }
    closedir(d);
}

/* ---------- Основная программа ---------- */

int main(int argc, char *argv[])
{
    int rc = EXIT_FAILURE;
    int sockfd = -1;
    int do_bind = 0;

    /* Построчная буферизация: вывод и сообщения об ошибках идут в правильном порядке */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* 1. Валидация аргументов командной строки */
    if (argc != 5 && argc != 6)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 6)
    {
        if (strcmp(argv[5], "--bind") != 0)
        {
            fprintf(stderr, "Ошибка: неизвестный флаг '%s' (ожидается --bind)\n", argv[5]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        do_bind = 1;
    }

    if (strcmp(argv[1], "AF_INET") != 0)
    {
        fprintf(stderr, "Ошибка: домен '%s' не поддерживается (допустим только AF_INET)\n", argv[1]);
        return EXIT_FAILURE;
    }

    int type;
    if (strcmp(argv[2], "SOCK_STREAM") == 0)
        type = SOCK_STREAM;
    else if (strcmp(argv[2], "SOCK_DGRAM") == 0)
        type = SOCK_DGRAM;
    else
    {
        fprintf(stderr, "Ошибка: неверный тип сокета '%s' (SOCK_STREAM или SOCK_DGRAM)\n", argv[2]);
        return EXIT_FAILURE;
    }

    uint16_t port;
    int pr = parse_port(argv[4], &port);
    if (pr == -1)
    {
        fprintf(stderr, "Ошибка: порт '%s' не является числом\n", argv[4]);
        return EXIT_FAILURE;
    }
    if (pr == -2)
    {
        fprintf(stderr, "Ошибка: порт '%s' вне диапазона 0..65535\n", argv[4]);
        return EXIT_FAILURE;
    }

    /* 2. Создание сокета: protocol = 0 - ядро выбирает протокол само */
    sockfd = socket(AF_INET, type, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }
    printf("Сокет создан. Файловый дескриптор: %d\n", sockfd);
    if (sockfd >= 3)
        printf("Проверка: дескриптор >= 3 (0, 1, 2 заняты stdin/stdout/stderr)\n");

    /* 3. Проверка типа и протокола, выбранных ядром (вариант 11) */
    int so_type = 0, so_proto = 0;
    socklen_t optlen = sizeof(so_type);
    if (getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &so_type, &optlen) < 0)
    {
        perror("getsockopt(SO_TYPE)");
        goto cleanup;
    }
    optlen = sizeof(so_proto);
    if (getsockopt(sockfd, SOL_SOCKET, SO_PROTOCOL, &so_proto, &optlen) < 0)
    {
        perror("getsockopt(SO_PROTOCOL)");
        goto cleanup;
    }
    printf("Тип сокета (SO_TYPE): %d (%s)\n", so_type,
           so_type == SOCK_STREAM ? "SOCK_STREAM" : "SOCK_DGRAM");
    printf("Протокол, выбранный ядром при protocol=0 (SO_PROTOCOL): %d (%s)\n",
           so_proto, proto_name(so_proto));

    if (type == SOCK_STREAM)
    {
        if (so_proto == IPPROTO_TCP)
            printf("ПРОВЕРКА ПРОЙДЕНА: SOCK_STREAM + protocol=0 -> ядро выбрало TCP\n");
        else
        {
            fprintf(stderr, "ПРОВЕРКА НЕ ПРОЙДЕНА: ожидался TCP, получен %d\n", so_proto);
            goto cleanup;
        }
    }
    else if (so_proto == IPPROTO_UDP)
        printf("Для SOCK_DGRAM + protocol=0 ядро выбрало UDP\n");

    /* 4. Заполнение структуры адреса */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); /* обязательно обнуляем, в т.ч. sin_zero */
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port); /* порт -> сетевой порядок байт */

    int ir = inet_pton(AF_INET, argv[3], &addr.sin_addr);
    if (ir == 0)
    {
        fprintf(stderr, "Ошибка: '%s' - неверный формат IPv4-адреса (inet_pton вернул 0)\n", argv[3]);
        goto cleanup;
    }
    if (ir < 0)
    {
        perror("inet_pton");
        goto cleanup;
    }

    /* 5. Демонстрация порядка байт и обратное преобразование адреса */
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str)) == NULL)
    {
        perror("inet_ntop");
        goto cleanup;
    }
    printf("Адрес: %s:%u\n", ip_str, (unsigned)ntohs(addr.sin_port));
    printf("Порт в порядке хоста : %u (0x%04X)\n", (unsigned)port, (unsigned)port);
    printf("Поле sin_port после htons (как число на x86): 0x%04X - байты переставлены\n",
           (unsigned)addr.sin_port);

    print_fds("до close()");

    /* 6. Привязка (по флагу --bind) */
    if (do_bind)
    {
        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            perror("bind");
            if (errno == EADDRINUSE)
                fprintf(stderr, "Порт уже занят другим сокетом\n");
            else if (errno == EACCES)
                fprintf(stderr, "Нет прав на порт < 1024 (нужен root)\n");
            else if (errno == EADDRNOTAVAIL)
                fprintf(stderr, "Этот IP не принадлежит ни одному интерфейсу машины\n");
            goto cleanup;
        }

        /* Узнаём реальный адрес: важно при порте 0 (ядро выберет порт само) */
        struct sockaddr_in bound;
        socklen_t blen = sizeof(bound);
        if (getsockname(sockfd, (struct sockaddr *)&bound, &blen) < 0)
        {
            perror("getsockname");
            goto cleanup;
        }
        char bound_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &bound.sin_addr, bound_ip, sizeof(bound_ip));
        printf("bind() выполнен успешно: %s:%u\n", bound_ip, (unsigned)ntohs(bound.sin_port));

        /* Демонстрация EADDRINUSE: второй сокет на тот же адрес */
        int second = socket(AF_INET, type, 0);
        if (second < 0)
        {
            perror("socket (второй)");
            goto cleanup;
        }
        if (bind(second, (struct sockaddr *)&bound, sizeof(bound)) < 0)
        {
            if (errno == EADDRINUSE)
                printf("Второй bind() на тот же адрес -> EADDRINUSE (%s): ожидаемо\n", strerror(errno));
            else
                perror("bind (второй)");
        }
        else
            printf("Внимание: второй bind() неожиданно удался\n");
        close(second);
    }

    /* 7. Закрытие сокета и проверка освобождения дескриптора */
    int saved_fd = sockfd;
    if (close(sockfd) < 0)
    {
        perror("close");
        sockfd = -1;
        return EXIT_FAILURE;
    }
    sockfd = -1;
    printf("close() вернул 0. Дескриптор %d освобождён.\n", saved_fd);
    print_fds("после close()");
    return EXIT_SUCCESS;

cleanup:
    /* Единая точка очистки при ошибке: утечек дескрипторов нет */
    if (sockfd >= 0)
        close(sockfd);
    return rc;
}
