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
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define SERIAL "/dev/pts/7"
#define SPEED B9600
#define DB "./db.db"
#define HOUR 3600
// debug
#define HTTP_PORT 8080

int add_main_log(sqlite3 *db, double temp) {
  const char *sql = "INSERT INTO main_log (timestamp, temp) "
                    "VALUES (CURRENT_TIMESTAMP, ?);";

  sqlite3_stmt *stmt = NULL;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

  if (rc != SQLITE_OK) {
    printf("SQLite error: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  sqlite3_bind_double(stmt, 1, temp);

  rc = sqlite3_step(stmt);

  if (rc != SQLITE_DONE) {
    printf("SQLite error: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
  }

  sqlite3_finalize(stmt);
  return 0;
}
static int insert_value(sqlite3 *db, const char *sql, double temperature) {
  sqlite3_stmt *stmt = NULL;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

  if (rc != SQLITE_OK) {
    printf("SQLite error: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  sqlite3_bind_double(stmt, 1, temperature);

  rc = sqlite3_step(stmt);

  if (rc != SQLITE_DONE) {
    printf("SQLite insert error: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
  }

  sqlite3_finalize(stmt);
  return 0;
}

int add_hour_temp(sqlite3 *db, double avg_temp) {
  const char *sql = "INSERT INTO hour (avg_temp) VALUES (?);";

  return insert_value(db, sql, avg_temp);
}

int add_day_temp(sqlite3 *db, double avg_temp) {
  const char *sql = "INSERT INTO day (avg_temp) VALUES (?);";

  return insert_value(db, sql, avg_temp);
}
int clean_logs(sqlite3 *db) {
  const char *sql = "DELETE FROM main_log "
                    "WHERE timestamp < datetime('now', '-24 hours');"

                    "DELETE FROM hour "
                    "WHERE timestamp < datetime('now', '-1 month');"

                    "DELETE FROM day "
                    "WHERE timestamp < datetime('now', '-1year');";

  char *err_msg = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);

  if (rc != SQLITE_OK) {
    printf("SQLite cleanup error: %s\n", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  return 0;
}
static char *make_period_json(sqlite3 *db, const char *sql) {
  size_t cap = 4096;
  size_t len = 0;
  char *out = malloc(cap);
  len = snprintf(out, cap, "{\"data\":[");

  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

  int first = 1;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    char row[256];
    int n = snprintf(row, sizeof(row),
                     "%s{\"timestamp\":\"%s\",\"temperature\":%.2f}",
                     first ? "" : ",",
                     (const char *)sqlite3_column_text(stmt, 0),
                     sqlite3_column_double(stmt, 1));

    while (len + n + 3 > cap) {
      cap *= 2;
      out = realloc(out, cap);
    }
    memcpy(out + len, row, n);
    len += n;
    out[len] = '\0';
    first = 0;
  }
  sqlite3_finalize(stmt);

  strcpy(out + len, "]}");
  return out;
}

static void send_all(int fd, const char *data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
    if (n <= 0)
      return;
    sent += n;
  }
}

void handle(int client_fd, sqlite3 *db) {
  char request[1024] = {0};
  recv(client_fd, request, sizeof(request) - 1, 0);

  char *body = NULL;

  if (strstr(request, "GET /current")) {
    const char *sql = "SELECT timestamp, temp FROM main_log "
                      "ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    char tmp[256];
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      snprintf(tmp, sizeof(tmp),
               "{\"timestamp\":\"%s\",\"temperature\":%.2f}",
               (const char *)sqlite3_column_text(stmt, 0),
               sqlite3_column_double(stmt, 1));
    } else {
      snprintf(tmp, sizeof(tmp), "{\"error\":\"no data\"}");
    }
    sqlite3_finalize(stmt);
    body = strdup(tmp);

  } else if (strstr(request, "period=hour")) {
    body = make_period_json(
        db, "SELECT timestamp, avg_temp FROM hour ORDER BY timestamp;");

  } else if (strstr(request, "period=day")) {
    body = make_period_json(
        db, "SELECT timestamp, avg_temp FROM day ORDER BY timestamp;");

  } else {
    body = strdup("{\"error\":\"unknown request\"}");
  }

  char header[256];
  int hlen = snprintf(header, sizeof(header),
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));

  send_all(client_fd, header, hlen);
  send_all(client_fd, body, strlen(body));
  free(body);
}

void *http_server(void *arg) {
  (void)arg;

  sqlite3 *db = NULL;

  if (sqlite3_open(DB, &db) != SQLITE_OK) {
    fprintf(stderr, "sqlite error\n");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return NULL;
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket");
    sqlite3_close(db);
    return NULL;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(HTTP_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    close(server_fd);
    sqlite3_close(db);
    return NULL;
  }

  if (listen(server_fd, 10) == -1) {
    perror("listen");
    close(server_fd);
    sqlite3_close(db);
    return NULL;
  }

  printf("on port %d\n", HTTP_PORT);

  while (1) {
    int client_fd = accept(server_fd, NULL, NULL);

    if (client_fd == -1) {
      perror("accept");
      continue;
    }

    handle(client_fd, db);
    close(client_fd);
  }

  sqlite3_close(db);
  close(server_fd);
  return NULL;
}
int main() {
  char buf_tmpl[32];
  int TEMPLATE_SIZE = snprintf(buf_tmpl, sizeof(buf_tmpl), "%.2f\n", 25.0);
  int serial_fd = open(SERIAL, O_RDONLY, 0777);
  if (serial_fd == -1) {
    printf("serial error");
    return -1;
  }

  sqlite3 *db = NULL;

  int rc = sqlite3_open(DB, &db);
  if (rc != SQLITE_OK) {
    printf("sqlite error %d", rc);
    return -1;
  }
  const char *sql = "CREATE TABLE IF NOT EXISTS main_log ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "timestamp DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                    "temp REAL NOT NULL"
                    ");"

                    "CREATE TABLE IF NOT EXISTS hour ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "timestamp DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                    "avg_temp REAL NOT NULL,"
                    "UNIQUE(timestamp)"
                    ");"

                    "CREATE TABLE IF NOT EXISTS day ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "timestamp DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                    "avg_temp REAL NOT NULL,"
                    "UNIQUE(timestamp)"
                    ");";

  char *err_msg = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);

  if (rc != SQLITE_OK) {
    printf("sql error: %s\n", err_msg);

    sqlite3_free(err_msg);
    return -1;
  }

  pthread_t http_thread;
  if (pthread_create(&http_thread, NULL, http_server, NULL) != 0) {
    printf("pthread error");
    close(serial_fd);
    sqlite3_close(db);
    return -1;
  }
  pthread_detach(http_thread);

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
    add_main_log(db, strtod(buf, NULL));

    count_hour += 1;
    curr_hour += (strtod(buf, NULL) - curr_hour) / count_hour;
    count_day += 1;
    curr_day += (strtod(buf, NULL) - curr_day) / count_day;
    if (time(NULL) - last_hour >= HOUR) {

      add_hour_temp(db, curr_hour);
      last_hour = time(NULL);
      count_hour = 0;
      curr_hour = 0.0;
    }
    if (time(NULL) - last_day >= 24 * HOUR) {
      add_day_temp(db, curr_day);
      last_day = time(NULL);
      count_day = 0;
      curr_day = 0.0;
    }
    clean_logs(db);
  }

  close(serial_fd);
  sqlite3_close(db);
  return 0;
}
