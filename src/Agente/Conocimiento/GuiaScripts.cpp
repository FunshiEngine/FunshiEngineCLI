/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Conocimiento/GuiaScripts.h"

#include <FileManager/FileManager.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace funshi::agente::conocimiento {

namespace {

std::string extension(const std::string& ruta) {
    std::string e = std::filesystem::path(ruta).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// La API que el motor inyecta a los scripts (BackendJava: clase Nativo;
// ScriptGameObject.h: ApiScriptGameObject y ScriptServices), lo más usado.
const char* kApiJava =
    "- Posición, rotación y escala: Nativo.posicionX(objeto) (también Y, Z), "
    "Nativo.fijarPosicion(objeto, x, y, z), Nativo.fijarRotacion(objeto, angulo, ejeX, ejeY, ejeZ), "
    "Nativo.fijarEscala(objeto, x, y, z).\n"
    "- Física: Nativo.fijarVelocidadHorizontal(objeto, x, z), Nativo.saltar(objeto, velocidad), "
    "Nativo.agregarRigidBody(objeto, masa), Nativo.fijarMasa(objeto, masa), Nativo.fijarUsoGravedad(objeto, true).\n"
    "- Teclado (nombres GLFW: \"W\", \"A\", \"SPACE\", \"LEFT_SHIFT\", \"UP\"): Nativo.teclaSostiene(\"W\"), "
    "Nativo.teclaPresionada(\"SPACE\"), Nativo.teclaSoltada(\"E\"); mouse: Nativo.deltaMouseX().\n"
    "- Objetos: Nativo.nombre(objeto), Nativo.objetoPorNombre(\"Jugador\"), Nativo.objetoPorEtiqueta(\"Enemigo\"), "
    "Nativo.tieneEtiqueta(objeto, \"Enemigo\"), Nativo.crearObjeto(\"Bala\", 0), Nativo.destruirObjeto(objeto), "
    "Nativo.objetoDeCollider(otroCollider).\n"
    "- Consola y sonido: Nativo.imprimir(\"texto\"), Nativo.reproducirSonido(\"Sonidos/salto.wav\", 1.0f, false).\n";

const char* kApiCpp =
    "- Objeto (api, puede ser nulo): api->posicionX(owner) (también Y, Z), api->fijarPosicion(owner, x, y, z), "
    "api->fijarRotacionEjes(owner, angulo, x, y, z), api->fijarEscala(owner, x, y, z), "
    "api->fijarVelocidadHorizontal(owner, x, z), api->saltar(owner, velocidad), api->tieneEtiqueta(owner, \"Enemigo\"), "
    "api->imprimirConsola(\"texto\").\n"
    "- Escena (servicios, puede ser nulo): servicios->teclaSostiene(\"W\"), servicios->teclaPresionada(\"SPACE\"), "
    "servicios->objetoPorNombre(\"Jugador\"), servicios->crearObjeto(\"Bala\", nullptr), "
    "servicios->reproducirSonido(\"Sonidos/salto.wav\", 1.0f, false).\n"
    "- Comprueba antes de usar: if (api) api->fijarPosicion(owner, x, y, z);\n";

const char* kEjemploJava =
    "public class Jugador implements Comportamiento {\n"
    "    public float velocidad = 5.0f;\n"
    "\n"
    "    @Override\n"
    "    public void iniciar(long objeto) {\n"
    "        Nativo.imprimir(\"Jugador listo\");\n"
    "    }\n"
    "\n"
    "    @Override\n"
    "    public void actualizar(long objeto, double deltaTime) {\n"
    "        float x = 0, z = 0;\n"
    "        if (Nativo.teclaSostiene(\"W\")) z -= velocidad;\n"
    "        if (Nativo.teclaSostiene(\"S\")) z += velocidad;\n"
    "        if (Nativo.teclaSostiene(\"A\")) x -= velocidad;\n"
    "        if (Nativo.teclaSostiene(\"D\")) x += velocidad;\n"
    "        Nativo.fijarVelocidadHorizontal(objeto, x, z);\n"
    "        if (Nativo.teclaPresionada(\"SPACE\")) Nativo.saltar(objeto, 6.0f);\n"
    "    }\n"
    "}\n";

const char* kEjemploCpp =
    "    void onUpdate(GameObject* owner, float deltaTime) override {\n"
    "        (void)deltaTime;\n"
    "        if (!api || !servicios) return;\n"
    "        float x = 0, z = 0;\n"
    "        if (servicios->teclaSostiene(\"W\")) z -= velocidad;\n"
    "        if (servicios->teclaSostiene(\"D\")) x += velocidad;\n"
    "        api->fijarVelocidadHorizontal(owner, x, z);\n"
    "    }\n";

// true si las llaves están balanceadas (fuera de cadenas y comentarios).
bool llavesBalanceadas(const std::string& c) {
    int nivel = 0;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const char x = c[i];
        if (x == '/' && i + 1 < c.size() && c[i + 1] == '/') {
            i = c.find('\n', i);
            if (i == std::string::npos) break;
        } else if (x == '/' && i + 1 < c.size() && c[i + 1] == '*') {
            i = c.find("*/", i + 2);
            if (i == std::string::npos) break;
            ++i;
        } else if (x == '"' || x == '\'') {
            for (++i; i < c.size() && c[i] != x; ++i) {
                if (c[i] == '\\') ++i;
            }
        } else if (x == '{') {
            ++nivel;
        } else if (x == '}' && --nivel < 0) {
            return false;
        }
    }
    return nivel == 0;
}

}  // namespace

std::string documentoScripts() {
    std::string s =
        "Un script es un archivo en src/ del proyecto (p. ej. Scripts/Jugador.java o Scripts/Enemigo.cpp) que el "
        "motor carga como comportamiento de un objeto. Solo Java (.java) y C++ (.cpp).\n"
        "Pasos: 1) project.src.create con la ruta y el contenido completo; 2) script.compile; 3) "
        "scene.object.addcomponent con componentType Script y value = la ruta tal como la devuelve script.list.\n\n"
        "### Java (.java)\n"
        "- La clase se llama igual que el archivo: Scripts/Jugador.java → public class Jugador.\n"
        "- Implementa Comportamiento: iniciar(long objeto), actualizar(long objeto, double deltaTime); "
        "opcionales detener(long objeto) y colisionInicio/colisionPersistencia/colisionFin(long objeto, long collider, "
        "long otroCollider).\n"
        "- `objeto` es un número (long) que identifica al objeto: se le pasa a Nativo.*.\n"
        "- Sin package, sin import del motor, sin main. Comportamiento y Nativo ya existen.\n"
        "- Los campos public (public float velocidad = 5.0f;) se editan en el inspector y con scene.object.setfield.\n"
        "API (clase Nativo, métodos estáticos):\n";
    s += kApiJava;
    s += "Plantilla del motor:\n```java\n" + FileManager::plantillaScript("Jugador", true) + "```\n";
    s += "Ejemplo (moverse con WASD y saltar con espacio):\n```java\n" + std::string(kEjemploJava) + "```\n\n";
    s += "### C++ (.cpp)\n"
         "- #include \"Behaviour/IScriptBehaviour.h\"; la clase se llama FUNSHI_NOMBRE_CLASE (el motor la renombra con "
         "el nombre del archivo) y hereda de IScriptBehaviour.\n"
         "- Métodos: onStart(GameObject* owner), onUpdate(GameObject* owner, float deltaTime), onStop(GameObject* "
         "owner); opcionales onCollisionEnter/Stay/Exit(GameObject* owner, Collider* propio, Collider* otro).\n"
         "- Campos editables con REFLECT_INICIO(FUNSHI_NOMBRE_CLASE) REFLECT_CAMPO(velocidad) REFLECT_FIN.\n"
         "- Al final, sin cambiarla, la fábrica extern \"C\" FUNSHI_COMPORTAMIENTO_EXPORT IScriptBehaviour* "
         "FUNSHI_CREAR_COMPORTAMIENTO(...).\n"
         "API:\n";
    s += kApiCpp;
    s += "Plantilla del motor:\n```cpp\n" + FileManager::plantillaScript("Enemigo", false) + "```\n";
    s += "Ejemplo de onUpdate:\n```cpp\n" + std::string(kEjemploCpp) + "```\n";
    return s;
}

std::string plantillaDeScript(const std::string& ruta) {
    const std::string e = extension(ruta);
    if (e != ".java" && e != ".cpp") return {};
    return FileManager::plantillaScript(std::filesystem::path(ruta).stem().string(), e == ".java");
}

std::string problemaDeScript(const std::string& ruta, const std::string& contenido) {
    const std::string e = extension(ruta);
    const std::string clase = std::filesystem::path(ruta).stem().string();
    if (e == ".java") {
        static const std::regex paquete(R"((^|\n)\s*package\s+[\w.]+\s*;)");
        static const std::regex otroMotor(R"(MonoBehaviour|UnityEngine|extends\s+\w*Behaviour)");
        static const std::regex principal(R"(static\s+void\s+main\s*\()");
        static const std::regex publica(R"(public\s+(?:final\s+|abstract\s+)?class\s+(\w+))");
        std::smatch m;
        if (std::regex_search(contenido, paquete)) return "un script Java del motor no lleva package";
        if (std::regex_search(contenido, otroMotor)) return "eso es de otro motor: aquí la clase implementa Comportamiento";
        if (std::regex_search(contenido, principal)) return "el motor no ejecuta main: usa iniciar y actualizar";
        if (std::regex_search(contenido, m, publica) && m[1].str() != clase) {
            return "la clase pública se llama " + m[1].str() + " y el archivo " + clase + ".java: deben llamarse igual";
        }
        if (!std::regex_search(contenido, std::regex("class\\s+" + clase + "\\b"))) {
            return "falta la clase " + clase + " (public class " + clase + " implements Comportamiento)";
        }
        if (!llavesBalanceadas(contenido)) return "las llaves { } no están balanceadas";
        if (contenido.find("implements Comportamiento") != std::string::npos &&
            (!std::regex_search(contenido, std::regex(R"(void\s+iniciar\s*\()")) ||
             !std::regex_search(contenido, std::regex(R"(void\s+actualizar\s*\()")))) {
            return "un Comportamiento necesita iniciar(long objeto) y actualizar(long objeto, double deltaTime)";
        }
        if (contenido.find("Comportamiento") == std::string::npos &&
            std::regex_search(contenido, std::regex(R"(void\s+(start|update|Start|Update|onStart|onUpdate)\s*\()"))) {
            return "un script de objeto implementa Comportamiento (iniciar/actualizar), no start/update";
        }
        return {};
    }
    if (e == ".cpp") {
        if (contenido.find("IScriptBehaviour") == std::string::npos) return "la clase debe heredar de IScriptBehaviour";
        if (contenido.find("FUNSHI_CREAR_COMPORTAMIENTO") == std::string::npos) {
            return "falta la fábrica FUNSHI_CREAR_COMPORTAMIENTO al final (sin ella el motor no carga el script)";
        }
        if (!llavesBalanceadas(contenido)) return "las llaves { } no están balanceadas";
        return {};
    }
    return {};
}

}  // namespace funshi::agente::conocimiento
