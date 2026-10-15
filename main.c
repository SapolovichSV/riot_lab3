/*
"Используется устройство, которое публикует по серийному порту, интерфейсу USB,
либо любому другому (I2C, SPI, CAN) текущую температуру окружающей среды. Если
такого устройства у вас нет, необходимо написать его программный эмулятор.
Необходимо написать на C\C++ (можно с использованием библиотек Boost или Qt)
программу для POSIX ОС (Linux), которая считывает информацию с порта и
записывает ее в лог-файл (или БД). Каждый час программа должна считать среднюю
температуру за час и записывать ее в другой лог-файл, а раз в день – среднюю
температуру за день, записывая ее в 3й лог-файл.
Основной лог-файл должен
хранить только измерения за последние 24 часа, лог-файл со средней температурой
за час должен хранить только измерения за последний месяц, лог файл со средней
дневной температурой накапливает информацию за текущий год. По HTTP-запросу
программа должна публиковать текущую температуру, а также статистику за
выбранный период времени. Также необходимо написать на C\C++ (можно с
использованием библиотеки Qt) программу-клиента с графическим интерфейсом,
которая получает данные от сервера по HTTP-запросу и отображает текущее значение
температуры и график изменения температуры за выбранный период. На проверку:
исходные коды программ"
*/
#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#define SERIAL "/dev/pts/16"
#define SPEED B9600
#define MAIN_LOG "./main_log"
// только ласт 24 часа
#define AVG_HOUR_LOG "./avg_hour_log"
// только ласт месяц
#define AVG_DAY_LOG "./avg_day_log"
// только ласт год

#define _XOPEN_SOURCE 700

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main() {
  char buf_tmpl[32];
  int TEMPLATE_SIZE = snprintf(buf_tmpl, sizeof(buf_tmpl), "%.2f\n", 25.0);
  int serial_fd = open(SERIAL, O_RDONLY, 0777);
  if (serial_fd == -1) {
    return -1;
  }
  int main_fd = open(MAIN_LOG, O_CREAT | O_RDWR, 0777);
  if (main_fd == -1) {
    return -1;
  }
  int hour_fd = open(AVG_HOUR_LOG, O_CREAT | O_RDWR, 0777);
  if (hour_fd == -1) {
    return -1;
  }
  int day_fd = open(AVG_DAY_LOG, O_CREAT | O_RDWR, 0777);
  if (day_fd == -1) {
    return -1;
  }

  time_t last_hour = time(NULL);
  double curr_hour = 0.0;
  int count_hour = 0;

  time_t last_day = time(NULL);
  double curr_day = 0.0;
  int count_day = 0;

  while (1) {
    char buf[32];
    int len = read(serial_fd, buf, TEMPLATE_SIZE);
    if (len == 0) {
      continue;
    }
    if (len == -1) {
      break;
    }
    if (len != TEMPLATE_SIZE) {
      printf("wtf");
      return -1;
    }
    buf[len] = '\0';

    write(main_fd, buf, len);

    char buf_log[1024];

    snprintf(buf_log, sizeof(buf), "%s %s",now,buf);



    last_hour = time(NULL);
    count_hour += 1;
    curr_hour += (strtod(buf, NULL) - curr_hour) / count_hour;
    count_day+= 1;
    curr_day+= (strtod(buf, NULL) - curr_day) / count_day;
    if (time(NULL) - last_hour >= 3600) {
      char buf_hour[32];
      int len = snprintf(buf_hour, sizeof(buf_hour), "%.2f\n", curr_hour);
      write(hour_fd, buf_hour, len);
      last_hour = time(NULL);
      count_hour = 0;
      curr_hour = 0.0;
    }
    if (time(NULL) - last_day >= 24 * 3600) {
      char buf_day[32];
      int len = snprintf(buf_day, sizeof(buf_day), "%.2f\n", curr_day);
      write(day_fd,buf_day,len);
      last_day = time(NULL);

    }
  }

  return 0;
}
