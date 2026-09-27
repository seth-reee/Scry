#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>

struct Mesh {
    // Six floats per vertex: position and face normal.
    QVector<float> data;
    QVector3D minimum;
    QVector3D maximum;
    qsizetype triangles = 0;
};

bool loadMesh(const QString &path, Mesh &mesh, QString &error);
