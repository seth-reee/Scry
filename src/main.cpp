#include "mesh.h"
#include "graphics.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QIcon>
#include <QMainWindow>
#include <QMatrix4x4>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QTextStream>
#include <QTimer>
#include <cmath>
#include <memory>

struct Palette {
    QColor background {"#121212"};
    QColor panel {"#1e1e1e"};
    QColor foreground {"#bebebe"};
    QColor accent {"#e68e0d"};
    QColor muted {"#333333"};
    QColor selection {"#2a2a2a"};
    QColor disabled {"#555555"};
};

static Palette readPalette()
{
    Palette p;
    QFile file(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return p;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed().remove('"').remove('\'');
        const QColor color(value);
        if (!color.isValid()) continue;
        if (key == "background") p.background = color;
        else if (key == "lighter_background") p.panel = color;
        else if (key == "foreground") p.foreground = color;
        else if (key == "accent") p.accent = color;
        else if (key == "muted") p.muted = color;
        else if (key == "selection") p.selection = color;
        else if (key == "dark_foreground") p.disabled = color;
    }
    return p;
}

class View final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit View(QWidget *parent = nullptr) : QOpenGLWidget(parent) { setFocusPolicy(Qt::StrongFocus); }
    ~View() override { cleanup(); }

    QString renderingError() const { return graphicsError; }
    bool isRendererReady() const { return rendererReady; }
    qsizetype triangleCount() const { return model.triangles; }

    void setPaletteColors(const Palette &p) { colors = p; update(); }
    void setMesh(Mesh mesh)
    {
        model = std::move(mesh);
        pendingUpload = true;
        resetView();
    }
    void resetView() { yaw = -35.f; pitch = 24.f; distance = 4.5f; update(); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QOpenGLWidget::paintEvent(event);
        if (!isValid() && graphicsError.isEmpty())
            failGraphics("The graphics driver could not create an OpenGL context for the model view.");
    }

    void initializeGL() override
    {
        cleanupConnection = connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                                    this, &View::cleanup, Qt::DirectConnection);
        if (!initializeOpenGLFunctions()) {
            failGraphics("Could not initialize desktop OpenGL 3.3 functions.");
            return;
        }
        program = std::make_unique<QOpenGLShaderProgram>();
        glEnable(GL_DEPTH_TEST);
        const char *vertex = R"(
            #version 330 core
            layout(location=0) in vec3 position;
            layout(location=1) in vec3 normal;
            uniform mat4 mvp;
            uniform mat3 normalMatrix;
            out vec3 n;
            void main() { gl_Position = mvp * vec4(position, 1.0); n = normalize(normalMatrix * normal); }
        )";
        const char *fragment = R"(
            #version 330 core
            in vec3 n;
            uniform vec3 baseColor;
            out vec4 color;
            void main() {
                float light = 0.34 + 0.66 * max(dot(normalize(n), normalize(vec3(0.35, 0.7, 1.0))), 0.0);
                color = vec4(baseColor * light, 1.0);
            }
        )";
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
            !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) ||
            !program->link()) {
            failGraphics("Could not compile the model renderer:\n" + program->log());
            return;
        }
        if (!vao.create() || !buffer.create()) {
            failGraphics("Could not create GPU buffers for the model renderer.");
            return;
        }
        graphicsError.clear();
        rendererReady = true;
        pendingUpload = true;
    }

    void paintGL() override
    {
        if (!rendererReady) {
            QPainter painter(this);
            painter.fillRect(rect(), colors.background);
            painter.setPen(colors.foreground);
            painter.drawText(rect().adjusted(30, 30, -30, -30), Qt::AlignCenter | Qt::TextWordWrap,
                             graphicsError + "\n\n" + graphicsRecoveryAdvice());
            return;
        }
        // QPainter used by the empty view can change OpenGL state between frames.
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClearColor(colors.background.redF(), colors.background.greenF(), colors.background.blueF(), 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (pendingUpload) {
            vao.bind();
            buffer.bind();
            buffer.allocate(model.data.constData(), int(model.data.size() * sizeof(float)));
            GLint allocatedBytes = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocatedBytes);
            if (allocatedBytes != model.data.size() * qsizetype(sizeof(float))) {
                buffer.release();
                vao.release();
                failGraphics("The graphics driver could not upload the model to GPU memory.");
                return;
            }
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
            buffer.release();
            vao.release();
            pendingUpload = false;
        }
        if (model.triangles == 0) {
            QPainter painter(this);
            painter.setPen(colors.foreground);
            painter.drawText(rect(), Qt::AlignCenter, "Open or drop an STL / 3MF file\n\nDrag to rotate  ·  Wheel to zoom");
            return;
        }
        QMatrix4x4 projection;
        projection.perspective(45.f, float(width()) / qMax(1, height()), 0.01f, 100.f);
        QMatrix4x4 view;
        view.translate(0, 0, -distance);
        view.rotate(pitch, 1, 0, 0);
        view.rotate(yaw, 0, 1, 0);
        QMatrix4x4 object;
        const QVector3D center = (model.minimum + model.maximum) * 0.5f;
        const QVector3D extent = model.maximum - model.minimum;
        const float size = qMax(qMax(extent.x(), extent.y()), qMax(extent.z(), 0.000001f));
        object.scale(2.f / size);
        object.translate(-center);
        program->bind();
        program->setUniformValue("mvp", projection * view * object);
        program->setUniformValue("normalMatrix", (view * object).normalMatrix());
        program->setUniformValue("baseColor", QVector3D(colors.accent.redF(), colors.accent.greenF(), colors.accent.blueF()));
        vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, int(model.data.size() / 6));
        vao.release();
        program->release();
    }

    void mousePressEvent(QMouseEvent *event) override { lastPoint = event->position(); }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            const QPointF delta = event->position() - lastPoint;
            yaw += float(delta.x()) * 0.5f;
            pitch += float(delta.y()) * 0.5f;
            update();
        }
        lastPoint = event->position();
    }
    void wheelEvent(QWheelEvent *event) override
    {
        distance = qBound(1.3f, distance * std::pow(0.88f, event->angleDelta().y() / 120.f), 40.f);
        update();
    }

private:
    void failGraphics(const QString &error)
    {
        graphicsError = error;
        rendererReady = false;
        QTextStream(stderr) << error << '\n';
        if (qApp->property("scryDiagnostics").toBool()) return;
        QTimer::singleShot(0, this, [this] {
            if (showGraphicsFailure(this, graphicsError)) qApp->quit();
        });
    }

    void cleanup()
    {
        disconnect(cleanupConnection);
        if (context() && context()->isValid()) {
            makeCurrent();
            buffer.destroy();
            vao.destroy();
            program.reset();
            doneCurrent();
        }
        rendererReady = false;
        pendingUpload = true;
    }

    Mesh model;
    Palette colors;
    std::unique_ptr<QOpenGLShaderProgram> program;
    QOpenGLBuffer buffer {QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao;
    bool pendingUpload = false;
    bool rendererReady = false;
    QString graphicsError;
    QMetaObject::Connection cleanupConnection;
    float yaw = -35.f, pitch = 24.f, distance = 4.5f;
    QPointF lastPoint;
};

class Window final : public QMainWindow {
public:
    Window()
    {
        setWindowTitle("Scry");
        resize(1000, 700);
        setAcceptDrops(true);
        view = new View(this);
        setCentralWidget(view);
        auto *toolbar = addToolBar("Viewer");
        toolbar->setMovable(false);
        QAction *openAction = toolbar->addAction("Open…");
        openAction->setShortcut(QKeySequence::Open);
        connect(openAction, &QAction::triggered, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, "Open model", QString(), "3D models (*.stl *.STL *.3mf *.3MF)");
            if (!path.isEmpty()) open(path);
        });
        QAction *fitAction = toolbar->addAction("Reset view");
        fitAction->setShortcut(QKeySequence(Qt::Key_Home));
        connect(fitAction, &QAction::triggered, view, &View::resetView);
        auto *spacer = new QWidget(toolbar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(spacer);
        QAction *aboutAction = toolbar->addAction("About");
        connect(aboutAction, &QAction::triggered, this, &Window::showAbout);
        statusBar()->showMessage("Open or drop an STL or 3MF file");
        const QString themeDir = QDir::homePath() + "/.local/state/omarchy/current/theme";
        if (QFileInfo::exists(themeDir)) watcher.addPath(themeDir);
        connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { applyTheme(); });
        applyTheme();
    }

    bool open(const QString &path, bool showError = true)
    {
        Mesh mesh;
        QString error;
        if (!loadMesh(path, mesh, error)) {
            statusBar()->showMessage(error, 10000);
            if (showError) QMessageBox::warning(this, "Could not open model", error);
            else QTextStream(stderr) << error << '\n';
            return false;
        }
        const qsizetype triangles = mesh.triangles;
        view->setMesh(std::move(mesh));
        setWindowTitle(QFileInfo(path).fileName() + " — Scry");
        statusBar()->showMessage(QString("%1 triangles  ·  Drag to rotate  ·  Wheel to zoom").arg(triangles));
        return true;
    }

    View *modelView() const { return view; }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->urls().isEmpty()) open(event->mimeData()->urls().first().toLocalFile());
    }

private:
    void showAbout()
    {
        QDialog dialog(this);
        dialog.setWindowTitle("About Scry");
        dialog.resize(580, 420);
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(10);

        auto *icon = new QLabel;
        icon->setPixmap(qApp->windowIcon().pixmap(72, 72));
        layout->addWidget(icon);
        auto *heading = new QLabel("Scry " + qApp->applicationVersion());
        QFont font = heading->font();
        font.setPointSize(17);
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        layout->addWidget(new QLabel("A lightweight STL and 3MF previewer."));

        githubLink = new QLabel;
        githubLink->setOpenExternalLinks(true);
        setGithubLinkColor(readPalette().accent);
        layout->addWidget(githubLink);
        layout->addWidget(new QLabel("MIT License · Copyright © 2026 seth-reee"));

        QFile license(":/LICENSE");
        auto *licenseText = new QTextBrowser;
        if (license.open(QIODevice::ReadOnly)) licenseText->setPlainText(QString::fromUtf8(license.readAll()));
        layout->addWidget(licenseText, 1);

        auto *close = new QPushButton("Close");
        auto *bottom = new QHBoxLayout;
        bottom->addStretch();
        bottom->addWidget(close);
        layout->addLayout(bottom);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }

    void setGithubLinkColor(const QColor &accent)
    {
        if (githubLink)
            githubLink->setText(QString("Created by seth-reee · <a href=\"https://github.com/seth-reee\" "
                                        "style=\"color: %1;\">GitHub profile</a>").arg(accent.name()));
    }

    void applyTheme()
    {
        const Palette p = readPalette();
        view->setPaletteColors(p);
        setGithubLinkColor(p.accent);
        QPalette palette = qApp->palette();
        palette.setColor(QPalette::Window, p.background);
        palette.setColor(QPalette::WindowText, p.foreground);
        palette.setColor(QPalette::Base, p.panel);
        palette.setColor(QPalette::AlternateBase, p.background);
        palette.setColor(QPalette::Text, p.foreground);
        palette.setColor(QPalette::Button, p.panel);
        palette.setColor(QPalette::ButtonText, p.foreground);
        palette.setColor(QPalette::Highlight, p.selection);
        palette.setColor(QPalette::HighlightedText, p.foreground);
        palette.setColor(QPalette::ToolTipBase, p.panel);
        palette.setColor(QPalette::ToolTipText, p.foreground);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, p.disabled);
        palette.setColor(QPalette::Disabled, QPalette::Text, p.disabled);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, p.disabled);
        qApp->setPalette(palette);
        qApp->setStyleSheet(QString(
            "QMainWindow, QToolBar, QStatusBar { background: %1; color: %2; } "
            "QDialog { background: %3; color: %2; } "
            "QToolButton { color: %2; background: transparent; padding: 6px 12px; } "
            "QToolButton:hover { background: %4; color: %5; } "
            "QPushButton, QLineEdit, QComboBox { padding: 7px 10px; border: 1px solid %4; border-radius: 6px; } "
            "QPushButton:hover { border-color: %5; } "
            "QTextBrowser, QTreeView, QListView { border: 1px solid %4; } "
            "QStatusBar { border-top: 1px solid %4; }")
            .arg(p.panel.name(), p.foreground.name(), p.background.name(),
                 p.muted.name(), p.accent.name()));
    }
    View *view = nullptr;
    QPointer<QLabel> githubLink;
    QFileSystemWatcher watcher;
};

static void configureParser(QCommandLineParser &parser)
{
    parser.setApplicationDescription("STL and 3MF model previewer");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"diagnostics", "Print graphics diagnostics; optionally check a model render, then exit."});
    parser.addPositionalArgument("model", "STL or 3MF file to open.", "[model]");
}

int main(int argc, char **argv)
{
    // Help and version should also work on machines without a display server.
    if (argc == 2 && (QString::fromLocal8Bit(argv[1]) == "--version" ||
                      QString::fromLocal8Bit(argv[1]) == "-v" ||
                      QString::fromLocal8Bit(argv[1]) == "--help" ||
                      QString::fromLocal8Bit(argv[1]) == "-h")) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("scry");
        app.setApplicationVersion(SCRY_VERSION);
        QCommandLineParser parser;
        configureParser(parser);
        parser.process(app);
        return 0;
    }
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setApplicationName("scry");
    app.setApplicationVersion(SCRY_VERSION);
    app.setWindowIcon(QIcon(":/Scry.png"));
    QCommandLineParser parser;
    configureParser(parser);
    parser.process(app);
    const QStringList files = parser.positionalArguments();
    if (files.size() > 1) parser.showHelp(1);
    const bool diagnostics = parser.isSet("diagnostics");
    app.setProperty("scryDiagnostics", diagnostics);
    const GraphicsInfo graphics = probeGraphics();
    if (diagnostics) {
        QTextStream output(stdout);
        output << "Scry: " << app.applicationVersion() << "\nQt: " << qVersion()
               << "\nPlatform: " << app.platformName()
               << "\nOpenGL vendor: " << graphics.vendor
               << "\nOpenGL renderer: " << graphics.renderer
               << "\nOpenGL version: " << graphics.version
               << "\nOpenGL 3.3 available: " << (graphics.available ? "yes" : "no") << '\n';
        if (!graphics.error.isEmpty()) output << graphics.error << '\n';
        output.flush();
        if (!graphics.available || files.isEmpty()) return graphics.available ? 0 : 2;
    }
    if (!graphics.available) {
        QTextStream(stderr) << graphics.error << '\n';
        return showGraphicsFailure(nullptr, graphics.error) ? 0 : 2;
    }
    Window window;
    window.show();
    if (!files.isEmpty() && !window.open(files.first(), !diagnostics) && diagnostics) return 3;
    if (diagnostics) {
        QTimer::singleShot(500, &window, [&] {
            auto *view = window.modelView();
            const QImage frame = view->grabFramebuffer();
            const QColor background = readPalette().background;
            bool modelVisible = false;
            for (int y = 0; y < frame.height() && !modelVisible; ++y) {
                for (int x = 0; x < frame.width(); ++x) {
                    const QColor pixel = frame.pixelColor(x, y);
                    if (qAbs(pixel.red() - background.red()) > 2 ||
                        qAbs(pixel.green() - background.green()) > 2 ||
                        qAbs(pixel.blue() - background.blue()) > 2) {
                        modelVisible = true;
                        break;
                    }
                }
            }
            const bool passed = !frame.isNull() && view->isRendererReady() && modelVisible;
            QTextStream(stdout) << "Model render: " << (passed ? "passed" : "failed")
                                << "\nTriangles: " << view->triangleCount()
                                << "\nFramebuffer: " << frame.width() << 'x' << frame.height() << '\n';
            if (!view->renderingError().isEmpty()) QTextStream(stderr) << view->renderingError() << '\n';
            app.exit(passed ? 0 : 3);
        });
    }
    return app.exec();
}
