#include "graphics.h"

#include <QGuiApplication>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>

GraphicsInfo probeGraphics()
{
    GraphicsInfo info;
    QOpenGLContext context;
    context.setFormat(QSurfaceFormat::defaultFormat());
    if (!context.create()) {
        info.error = "The graphics driver could not create an OpenGL context.";
        return info;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid() || !context.makeCurrent(&surface)) {
        info.error = "The graphics driver could not activate an OpenGL context.";
        return info;
    }
    auto *functions = context.functions();
    auto string = [functions](GLenum name) {
        const auto *value = functions->glGetString(name);
        return value ? QString::fromLatin1(reinterpret_cast<const char *>(value)) : QString();
    };
    info.vendor = string(GL_VENDOR);
    info.renderer = string(GL_RENDERER);
    info.version = string(GL_VERSION);
    QOpenGLFunctions_3_3_Core core;
    if (context.isOpenGLES() || !core.initializeOpenGLFunctions())
        info.error = "Scry needs desktop OpenGL 3.3 or newer. The driver did not provide it.";
    else
        info.available = true;
    context.doneCurrent();
    return info;
}

QString graphicsRecoveryAdvice()
{
    QString advice = "Check that your GPU driver is installed and working.\n"
                     "Run scry --diagnostics in a terminal to collect graphics details.";
    if (QGuiApplication::platformName().startsWith("wayland"))
        advice += "\nOn Wayland, try the XWayland path: QT_QPA_PLATFORM=xcb scry";
    return advice;
}

bool showGraphicsFailure(QWidget *parent, const QString &reason)
{
    QMessageBox dialog(QMessageBox::Critical, "Graphics initialization failed",
                       reason + "\n\n" + graphicsRecoveryAdvice(), QMessageBox::Close, parent);
    QPushButton *retry = nullptr;
    if (QGuiApplication::platformName().startsWith("wayland") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        retry = dialog.addButton("Try XWayland", QMessageBox::ActionRole);
    dialog.exec();
    if (!retry || dialog.clickedButton() != retry) return false;

    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "xcb");
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(QCoreApplication::arguments().mid(1));
    if (process.startDetached()) return true;
    QMessageBox::critical(parent, "Could not restart Scry", process.errorString());
    return false;
}
