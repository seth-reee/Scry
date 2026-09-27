#include "mesh.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMatrix4x4>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtEndian>
#include <zip.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr qsizetype maxTriangles = 3'000'000;
constexpr qint64 maxFileBytes = 300ll * 1024 * 1024;
constexpr zip_uint64_t maxXmlBytes = 128ull * 1024 * 1024;

void addTriangle(Mesh &mesh, const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    const QVector3D normal = QVector3D::normal(a, b, c);
    for (const QVector3D &v : {a, b, c}) {
        if (mesh.triangles == 0 && mesh.data.isEmpty()) {
            mesh.minimum = mesh.maximum = v;
        } else {
            mesh.minimum.setX(qMin(mesh.minimum.x(), v.x()));
            mesh.minimum.setY(qMin(mesh.minimum.y(), v.y()));
            mesh.minimum.setZ(qMin(mesh.minimum.z(), v.z()));
            mesh.maximum.setX(qMax(mesh.maximum.x(), v.x()));
            mesh.maximum.setY(qMax(mesh.maximum.y(), v.y()));
            mesh.maximum.setZ(qMax(mesh.maximum.z(), v.z()));
        }
        mesh.data << v.x() << v.y() << v.z() << normal.x() << normal.y() << normal.z();
    }
    ++mesh.triangles;
}

bool finite(const QVector3D &v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

float littleFloat(const char *p)
{
    const quint32 bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool loadStl(const QString &path, Mesh &mesh, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.size() > maxFileBytes) {
        error = "File exceeds the 300 MB preview limit.";
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() >= 84) {
        const quint32 count = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + 80));
        if (count > 0 && quint64(bytes.size()) >= 84ull + 50ull * count) {
            if (count > maxTriangles) { error = "Model exceeds the 3 million triangle preview limit."; return false; }
            mesh.data.reserve(qsizetype(count) * 18);
            for (quint32 i = 0; i < count; ++i) {
                const char *p = bytes.constData() + 84 + 50 * qsizetype(i);
                const QVector3D a(littleFloat(p + 12), littleFloat(p + 16), littleFloat(p + 20));
                const QVector3D b(littleFloat(p + 24), littleFloat(p + 28), littleFloat(p + 32));
                const QVector3D c(littleFloat(p + 36), littleFloat(p + 40), littleFloat(p + 44));
                if (!finite(a) || !finite(b) || !finite(c)) {
                    error = "STL contains invalid coordinates.";
                    return false;
                }
                addTriangle(mesh, a, b, c);
            }
            return true;
        }
    }
    // ASCII STL: only vertex records matter for a geometric preview.
    const QList<QByteArray> lines = bytes.split('\n');
    QVector<QVector3D> triangle;
    for (const QByteArray &line : lines) {
        const QList<QByteArray> words = line.simplified().split(' ');
        if (words.size() != 4 || words[0] != "vertex") continue;
        bool xOk = false, yOk = false, zOk = false;
        QVector3D v(words[1].toFloat(&xOk), words[2].toFloat(&yOk), words[3].toFloat(&zOk));
        if (!xOk || !yOk || !zOk || !finite(v)) {
            error = "STL contains invalid coordinates.";
            return false;
        }
        triangle << v;
        if (triangle.size() == 3) {
            if (mesh.triangles >= maxTriangles) { error = "Model exceeds the 3 million triangle preview limit."; return false; }
            addTriangle(mesh, triangle[0], triangle[1], triangle[2]);
            triangle.clear();
        }
    }
    if (mesh.triangles == 0 || !triangle.isEmpty()) {
        error = "No complete triangles found in STL.";
        return false;
    }
    return true;
}

struct Component { int id = 0; QMatrix4x4 transform; };
struct Object {
    QVector<QVector3D> vertices;
    QVector<quint32> indices;
    QVector<Component> components;
};

QMatrix4x4 parseTransform(const QString &value)
{
    QMatrix4x4 m;
    const QStringList parts = value.simplified().split(' ', Qt::SkipEmptyParts);
    if (parts.size() != 12) return m;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            m(row, col) = parts[row * 4 + col].toFloat();
    return m;
}

bool readZipEntry(zip_t *archive, const QByteArray &name, QByteArray &out)
{
    zip_stat_t stat;
    if (zip_stat(archive, name.constData(), 0, &stat) != 0 || stat.size > maxXmlBytes) return false;
    zip_file_t *entry = zip_fopen(archive, name.constData(), 0);
    if (!entry) return false;
    out.resize(qsizetype(stat.size));
    zip_uint64_t read = 0;
    while (read < stat.size) {
        const zip_int64_t n = zip_fread(entry, out.data() + read, stat.size - read);
        if (n <= 0) { zip_fclose(entry); return false; }
        read += zip_uint64_t(n);
    }
    zip_fclose(entry);
    return true;
}

void readGeometry(QXmlStreamReader &xml, Object &object)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("vertices")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("vertex")) {
                    const auto a = xml.attributes();
                    object.vertices << QVector3D(a.value("x").toFloat(), a.value("y").toFloat(), a.value("z").toFloat());
                }
                xml.skipCurrentElement();
            }
        } else if (xml.name() == QLatin1String("triangles")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("triangle")) {
                    const auto a = xml.attributes();
                    object.indices << a.value("v1").toUInt() << a.value("v2").toUInt() << a.value("v3").toUInt();
                }
                xml.skipCurrentElement();
            }
        } else xml.skipCurrentElement();
    }
}

void readObject(QXmlStreamReader &xml, Object &object)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("mesh")) readGeometry(xml, object);
        else if (xml.name() == QLatin1String("components")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("component")) {
                    Component c;
                    c.id = xml.attributes().value("objectid").toInt();
                    c.transform = parseTransform(xml.attributes().value("transform").toString());
                    object.components << c;
                }
                xml.skipCurrentElement();
            }
        } else xml.skipCurrentElement();
    }
}

bool appendObject(int id, const QMatrix4x4 &transform, const QHash<int, Object> &objects,
                  QSet<int> &active, Mesh &mesh, QString &error)
{
    if (!objects.contains(id) || active.contains(id)) { error = "3MF has a missing or circular object reference."; return false; }
    active.insert(id);
    const Object &object = objects[id];
    for (qsizetype i = 0; i + 2 < object.indices.size(); i += 3) {
        if (mesh.triangles >= maxTriangles) { error = "Model exceeds the 3 million triangle preview limit."; return false; }
        const auto a = object.indices[i], b = object.indices[i + 1], c = object.indices[i + 2];
        if (a >= quint32(object.vertices.size()) || b >= quint32(object.vertices.size()) || c >= quint32(object.vertices.size())) {
            error = "3MF triangle refers to a missing vertex."; return false;
        }
        const QVector3D va = transform.map(object.vertices[a]);
        const QVector3D vb = transform.map(object.vertices[b]);
        const QVector3D vc = transform.map(object.vertices[c]);
        if (!finite(va) || !finite(vb) || !finite(vc)) { error = "3MF contains invalid coordinates."; return false; }
        addTriangle(mesh, va, vb, vc);
    }
    for (const Component &c : object.components)
        if (!appendObject(c.id, transform * c.transform, objects, active, mesh, error)) return false;
    active.remove(id);
    return true;
}

bool load3mf(const QString &path, Mesh &mesh, QString &error)
{
    int zipError = 0;
    zip_t *archive = zip_open(QFile::encodeName(path).constData(), ZIP_RDONLY, &zipError);
    if (!archive) { error = "Could not open 3MF package."; return false; }
    QByteArray modelName;
    QByteArray relationships;
    if (readZipEntry(archive, "_rels/.rels", relationships)) {
        QXmlStreamReader rel(relationships);
        while (!rel.atEnd()) {
            rel.readNext();
            if (rel.isStartElement() && rel.name() == QLatin1String("Relationship") &&
                rel.attributes().value("Type").toString().contains("3dmodel")) {
                modelName = rel.attributes().value("Target").toString().toUtf8();
                if (modelName.startsWith('/')) modelName.remove(0, 1);
                break;
            }
        }
    }
    if (modelName.isEmpty()) {
        const zip_int64_t count = zip_get_num_entries(archive, 0);
        for (zip_int64_t i = 0; i < count; ++i) {
            const QByteArray name(zip_get_name(archive, zip_uint64_t(i), 0));
            if (name.endsWith(".model")) { modelName = name; break; }
        }
    }
    QByteArray modelXml;
    const bool readOk = !modelName.isEmpty() && readZipEntry(archive, modelName, modelXml);
    zip_close(archive);
    if (!readOk) { error = "No readable 3D model found in 3MF package (128 MB XML limit)."; return false; }

    QXmlStreamReader xml(modelXml);
    QHash<int, Object> objects;
    QVector<Component> build;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == QLatin1String("object")) {
            const int id = xml.attributes().value("id").toInt();
            Object object;
            readObject(xml, object);
            objects.insert(id, std::move(object));
        } else if (xml.name() == QLatin1String("item")) {
            Component item;
            item.id = xml.attributes().value("objectid").toInt();
            item.transform = parseTransform(xml.attributes().value("transform").toString());
            build << item;
        }
    }
    if (xml.hasError()) { error = "Invalid 3MF model XML: " + xml.errorString(); return false; }
    QSet<int> active;
    for (const Component &item : build)
        if (!appendObject(item.id, item.transform, objects, active, mesh, error)) return false;
    if (mesh.triangles == 0) { error = "3MF contains no printable triangles."; return false; }
    return true;
}
}

bool loadMesh(const QString &path, Mesh &mesh, QString &error)
{
    mesh = Mesh{};
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "stl") return loadStl(path, mesh, error);
    if (suffix == "3mf") return load3mf(path, mesh, error);
    error = "Choose an STL or 3MF file.";
    return false;
}
