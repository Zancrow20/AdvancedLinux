// kernel_stack.c — CLI утилита
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#define DEVICE_PATH "/dev/int_stack"
#define IOCTL_SET_SIZE _IOW('s', 1, int)

void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <command> [args]\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  set-size N\n");
    fprintf(stderr, "  push N\n");
    fprintf(stderr, "  pop\n");
    fprintf(stderr, "  unwind\n");
    exit(1);
}

void do_set_size(int fd, int size) {
    int ret = ioctl(fd, IOCTL_SET_SIZE, &size);
    if (ret < 0) {
        if (errno == EINVAL)
            fprintf(stderr, "ERROR: size should be > 0\n");
        else
            perror("ioctl");
        exit(1);
    }
    printf("OK\n");
}

void do_push(int fd, int value) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", value);
    
    if (write(fd, buf, n) < 0) {
        if (errno == ERANGE) {
            fprintf(stderr, "ERROR: stack is full\n");
            exit(ERANGE * -1);
        }
        perror("write");
        exit(1);
    }
}

void do_pop(int fd) {
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    
    if (n == 0) {
        printf("NULL\n");
    } else if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    } else {
        perror("read");
        exit(1);
    }
}

void do_unwind(int fd) {
    while (1) {
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        
        buf[n] = '\0';
        printf("%s", buf);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) usage(argv[0]);
    
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        int saved_errno = errno;
        
        if (saved_errno == ENODEV || saved_errno == ENOENT) {
            fprintf(stderr, "error: USB key is not inserted\n");
        } else {
            perror("open /dev/int_stack");
        }
        return 1;
    }
    
    if (strcmp(argv[1], "set-size") == 0) {
        if (argc != 3) usage(argv[0]);
        do_set_size(fd, atoi(argv[2]));
    } else if (strcmp(argv[1], "push") == 0) {
        if (argc != 3) usage(argv[0]);
        do_push(fd, atoi(argv[2]));
    } else if (strcmp(argv[1], "pop") == 0) {
        do_pop(fd);
    } else if (strcmp(argv[1], "unwind") == 0) {
        do_unwind(fd);
    } else {
        usage(argv[0]);
    }
    
    close(fd);
    return 0;
}
