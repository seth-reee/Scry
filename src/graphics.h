#pragma once

#include <QString>
class QWidget;

struct GraphicsInfo {
    bool available = false;
    QString error;
    QString vendor;
    QString renderer;
    QString version;
};

GraphicsInfo probeGraphics();
QString graphicsRecoveryAdvice();
bool showGraphicsFailure(QWidget *parent, const QString &reason);
