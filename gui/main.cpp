#include "qlabel.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QVector>

#define SERVER_URL "http://127.0.0.1:8080"

class Graph : public QWidget {
public:
  QVector<double> temps;
  QStringList times;

  Graph() { setMinimumSize(500, 300); }

  void SetData(const QVector<double> &t, const QStringList &tm) {
    temps = t;
    times = tm;
    update();
  }

  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);

    int left = 50, right = 20, top = 20, bottom = 30;
    int w = width() - left - right;
    int h = height() - top - bottom;

    p.setPen(Qt::black);
    p.drawLine(left, top, left, top + h);
    p.drawLine(left, top + h, left + w, top + h);

    if (temps.isEmpty()) {
      p.drawText(rect(), Qt::AlignCenter, "no data");
      return;
    }

    double min = temps[0], max = temps[0];
    for (int i = 0; i < temps.size(); i++) {
      if (temps[i] < min)
        min = temps[i];
      if (temps[i] > max)
        max = temps[i];
    }

    p.drawText(2, top + 10, QString::number(max, 'f', 1));
    p.drawText(2, top + h, QString::number(min, 'f', 1));

    p.drawText(left, height() - 10, times.first());
    p.drawText(left + w - 120, height() - 10, times.last());

    int n = temps.size();
    int prev_x = 0, prev_y = 0;
    for (int i = 0; i < n; i++) {
      int x = left + (n == 1 ? w / 2 : i * w / (n - 1));
      int y = top + h - (int)((temps[i] - min) / (max - min) * h);
      if (i > 0)
        p.drawLine(prev_x, prev_y, x, y);
      prev_x = x;
      prev_y = y;
    }
  }
};

QNetworkAccessManager *manager;
QLabel *temp_label;
Graph *graph;

void OnCurrTemp() {
  QNetworkReply *reply =
      manager->get(QNetworkRequest(QUrl(SERVER_URL "/current")));
  QObject::connect(reply, &QNetworkReply::finished, [reply]() {
    if (reply->error() != QNetworkReply::NoError) {
      temp_label->setText("err: " + reply->errorString());
      reply->deleteLater();
      return;
    }
    QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    temp_label->setText(QString("%1 °C  (%2)")
                            .arg(obj["temperature"].toDouble(), 0, 'f', 2)
                            .arg(obj["timestamp"].toString()));

    reply->deleteLater();
  });
}

void LoadPeriod(const QString &period) {
  QString url = QString(SERVER_URL) + "/?period=" + period;
  QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
  QObject::connect(reply, &QNetworkReply::finished, [reply]() {
    if (reply->error() != QNetworkReply::NoError) {
      temp_label->setText("err: " + reply->errorString());
      reply->deleteLater();
      return;
    }
    QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    QJsonArray arr = obj["data"].toArray();

    QVector<double> temps;
    QStringList times;
    for (int i = 0; i < arr.size(); i++) {
      QJsonObject row = arr[i].toObject();
      times.append(row["timestamp"].toString());
      temps.append(row["temperature"].toDouble());
    }
    graph->SetData(temps, times);
    reply->deleteLater();
  });
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  QWidget *widget = new QWidget;
  manager = new QNetworkAccessManager(widget);

  temp_label = new QLabel("curr temp:");
  graph = new Graph;

  QPushButton *curr_temp = new QPushButton("curr temp");
  QObject::connect(curr_temp, &QPushButton::clicked, OnCurrTemp);

  QPushButton *hour_temp = new QPushButton("hour temp");
  QObject::connect(hour_temp, &QPushButton::clicked,
                   []() { LoadPeriod("hour"); });

  QPushButton *day_temp = new QPushButton("day temp");
  QObject::connect(day_temp, &QPushButton::clicked,
                   []() { LoadPeriod("day"); });

  QVBoxLayout *layout = new QVBoxLayout(widget);
  layout->addWidget(temp_label);
  layout->addWidget(curr_temp);
  layout->addWidget(hour_temp);
  layout->addWidget(day_temp);
  layout->addWidget(graph);

  widget->show();
  return app.exec();
}
