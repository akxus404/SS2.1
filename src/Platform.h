#pragma once

#include <QString>

#include "SecretKey.h"

class QWidget;

namespace Platform {
// Windows: excluye la ventana de capturas de pantalla y grabaciones. Otras plataformas: sin efecto.
void setCaptureProtection(QWidget* w, bool on);
}  // namespace Platform

// Llave de dispositivo: el segundo factor, guardado FUERA de la carpeta de la bóveda.
//  - Windows: cifrada con DPAPI (ligada a tu cuenta de Windows; no se puede abrir en otro equipo ni usuario).
//  - Linux / macOS: archivo con permisos 0600 en la carpeta de configuración (ver README).
namespace DeviceKey {
bool exists();
bool load(SecretKey& out);
bool save(const SecretKey& key);
void remove();  // borrado seguro

// Archivo .sskey: copia de la llave para recuperación / modo «solo llave externa».
bool writeKeyFile(const QString& path, const SecretKey& key, QString* err);
bool readKeyFile(const QString& path, SecretKey& out, QString* err);
}  // namespace DeviceKey
