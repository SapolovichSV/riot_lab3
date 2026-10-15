
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#define SERIAL "/dev/pts/15"
int main() {
    int fd = open("/dev/pts/15", O_WRONLY);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    float temp = 20.0;

    while (1) {


        temp += 0.5;
        if (temp > 30.0)
            temp = 25.0;
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%.2f\n",temp);
        write(fd,buf,len);
        sleep(1);
    }

    close(fd);
    return 0;
}
