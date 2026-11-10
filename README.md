# Secure Space (SS) 2.1

Bóveda de archivos cifrada en C++17 (Qt 6 + libsodium). Lo que entra se guarda cifrado con nombres aleatorios,
se puede **usar dentro de SS** (editar texto, ver imágenes, reproducir audio y vídeo) sin dejar copias
descifradas en el disco, y lo que se elimina se destruye de forma definitiva.

## Compilar

**Linux (Debian/Ubuntu)**
```
sudo apt install build-essential cmake pkg-config qt6-base-dev qt6-multimedia-dev libsodium-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/SecureSpace
```
**Windows (vcpkg)**: `vcpkg install qt6-base qt6-multimedia libsodium` y
`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`.
**macOS**: `brew install qt libsodium cmake pkg-config` (+ `-DCMAKE_PREFIX_PATH=$(brew --prefix qt)`).

Si Qt Multimedia no está instalado, el programa compila igualmente pero sin reproducción de audio/vídeo.

## Modelo de seguridad

**Contraseña + llave (dos factores).** La clave de la bóveda sale de `Argon2id(contraseña)` combinada con una
**llave aleatoria de 256 bits**. Quien copie la carpeta a otro equipo no tiene la llave, así que no hay
contraseña que probar: la fuerza bruta es inabordable aunque la contraseña sea débil.
- *Modo cómodo (por defecto):* la llave se guarda en este equipo, fuera de la carpeta de la bóveda
  (Windows: protegida con DPAPI, ligada a tu cuenta; Linux/macOS: archivo `0600` en `~/.config/.sscfg`).
- *Modo máxima protección:* la llave solo existe en un archivo `.sskey` (p. ej. en un USB) que se elige en cada inicio.
- En el primer inicio se guarda un **archivo `.sskey` de recuperación**. Sin contraseña **y** sin llave no hay
  forma de recuperar nada: es intencional.

**Archivos.** Cada archivo se cifra con su propia clave aleatoria, en bloques independientes de 256 KiB
(XChaCha20-Poly1305, con detección de manipulación, reordenación y truncado). Esto permite descifrar *solo el
trozo que se necesita*, en RAM: así el vídeo permite saltar a cualquier minuto sin descifrar el archivo entero.
Las claves de archivo, nombres, tamaños y ajustes viven en un índice cifrado con relleno.

**Uso dentro de SS.** El visor descifra a memoria; nunca escribe temporales. Texto: editable y se guarda
cifrado (clave nueva en cada guardado). Imágenes: zoom y ajuste. Audio/vídeo: reproductor con barra de
posición. Al bloquear (manual, inactividad o minimizar) se cierran los visores, se guardan los cambios pendientes
y se borran las claves de la memoria.

**Importar.** Por cada importación se elige (o se fija en Configuración):
- *Dejar una copia sin cifrar* fuera de la bóveda, o
- *Destruir de forma segura el original*: antes se **verifica** la copia cifrada (hash del contenido) y solo
  entonces se elimina el original; si la verificación falla, el original no se toca.

**Eliminar / destruir.** La clave del archivo sale del índice, se sobrescribe el archivo cifrado (1/3/7 pasadas
aleatorias + 1 a ceros), se trunca, se renombra al azar y se borra. «Destruir todo» elimina primero la llave de
este equipo, la cabecera y el índice, y después todo lo demás; SS vuelve a la pantalla de primer inicio.
«Limpiar espacio libre» (Configuración) sobrescribe los restos de archivos borrados antes.

## Límites que conviene conocer

- En **SSD** y sistemas de archivos con copia-al-escribir/journaling, sobrescribir no garantiza físicamente
  que no queden restos de los *originales* borrados. Lo que SS hace mejor es que lo que está dentro de la bóveda
  nunca se guardó en claro; para los originales, combina el borrado con «Limpiar espacio libre» y cifrado de
  disco completo (BitLocker / LUKS / FileVault).
- El archivo de **paginación / swap** del sistema puede guardar fragmentos de lo que se ve en pantalla. Con
  cifrado de disco completo (o swap cifrado) queda cubierto.
- El **límite de intentos** protege contra quien use el programa; no puede frenar a alguien que ya controla tu
  sesión del sistema operativo ni a malware con acceso a la memoria mientras la bóveda está abierta.
- En Linux/macOS la llave de dispositivo es un archivo `0600` (no hardware): protege contra copiar la carpeta de
  la bóveda, no contra copiar todo tu perfil. Para equipos robados usa el modo «solo llave externa» + cifrado de disco.
- Los archivos **exportados** quedan sin cifrar fuera de la bóveda.
- Primera versión: **no se ha compilado ni probado** en el entorno donde se escribió; es probable que haga falta
  corregir algún detalle menor de API al compilar con tu versión de Qt.

## Estructura

`Vault` (núcleo cifrado) · `Platform` (llave de dispositivo, DPAPI, anti-captura) · `Viewer` (visor/editor) ·
`MainWindow` · `AuthDialogs` · `SettingsDialog` · `Strength` · `Theme` (paleta `#1e223d`, logo "SS") · `UiUtil`.
