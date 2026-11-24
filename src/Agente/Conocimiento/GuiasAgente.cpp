/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Recetas de varios pasos para el agente. Cada paso nombra el comando exacto
// y sus argumentos; los ids reales salen del resultado del paso anterior.
#include "Agente/Conocimiento/BaseConocimiento.h"

namespace funshi::agente::conocimiento {

const std::vector<Guia>& guiasIncluidas() {
    static const std::vector<Guia> guias = {
        {"guia.objeto-con-modelo", "Crear un objeto con un modelo 3D",
         "modelo 3d malla fbx obj gltf importar asset objeto visible",
         "1. asset.import {\"project\":P,\"source\":\"/ruta/absoluta/modelo.fbx\"} → devuelve path (ruta relativa).\n"
         "2. scene.object.create {\"project\":P,\"name\":\"Nombre\"} → devuelve id.\n"
         "3. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"Model\",\"value\":PATH_DEL_PASO_1}.\n"
         "4. scene.save {\"project\":P}."},
        {"guia.transformar", "Mover, rotar o escalar un objeto",
         "mover posicion rotar girar escalar tamano transform ubicar",
         "1. Si no sabes el id: scene.object.findbyname {\"project\":P,\"name\":\"X\"} (o findbytag).\n"
         "2. scene.object.settransform {\"project\":P,\"id\":ID,\"position\":[x,y,z]} "
         "(también \"rotation\":[x,y,z] y \"scale\":[x,y,z]; solo los que cambian).\n"
         "3. Para verificar: scene.object.getglobaltransform {\"project\":P,\"id\":ID}."},
        {"guia.script", "Agregar un script (comportamiento) a un objeto",
         "script codigo comportamiento cpp java logica movimiento compilar",
         "1. project.src.create {\"project\":P,\"path\":\"Scripts/Nombre.cpp\",\"file\":true,\"content\":\"...\"}.\n"
         "2. script.compile {\"project\":P} y revisa diagnostics.list si falla.\n"
         "3. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"Script\",\"value\":RUTA} "
         "usando la ruta tal como la devuelve script.list.\n"
         "4. scene.script.reload {\"project\":P,\"id\":ID}."},
        {"guia.fisica", "Dar física a un objeto (colisión y gravedad)",
         "fisica colision collider rigidbody gravedad masa cae choca",
         "1. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"Collider\","
         "\"value\":{\"tipo\":\"esfera\",\"radio\":0.5}} (tipo: \"esfera\" o \"cubo\").\n"
         "2. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"RigidBody\",\"masa\":1}.\n"
         "Sin Collider previo, RigidBody crea una esfera de radio 1."},
        {"guia.audio", "Agregar sonido a un objeto",
         "audio sonido musica clip efecto reproducir",
         "1. asset.import {\"project\":P,\"source\":\"/ruta/sonido.wav\"} → path.\n"
         "2. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"AudioSource\","
         "\"value\":PATH,\"volume\":0.8,\"loop\":false,\"autoplay\":true,\"espacial\":true}."},
        {"guia.jerarquia", "Organizar la jerarquía (padres e hijos)",
         "padre hijo jerarquia agrupar desvincular arbol",
         "- Ver el árbol: scene.hierarchy {\"project\":P}.\n"
         "- Hacer hijo: scene.object.setparent {\"project\":P,\"id\":HIJO,\"parentId\":PADRE} (0 = raíz).\n"
         "- Volver a la raíz: scene.object.unparent {\"project\":P,\"id\":ID}.\n"
         "- Crear directamente como hijo: scene.object.create {..,\"parentId\":PADRE}."},
        {"guia.explorar", "Explorar una escena y encontrar objetos",
         "explorar inspeccionar escena objetos listar encontrar buscar id",
         "- Todo: scene.objects {\"project\":P} (puede ser largo: preferí findbyname/findbytag si sabes qué buscas).\n"
         "- Un objeto: scene.object.get {\"project\":P,\"id\":ID}; sus componentes: scene.object.components.\n"
         "- Posición en el mundo: scene.object.getglobalpos / getglobaltransform."},
        {"guia.deshacer", "Deshacer o rehacer cambios en la escena",
         "deshacer rehacer error equivocacion volver atras undo redo",
         "- scene.undo {\"project\":P} deshace el último cambio; scene.redo lo rehace.\n"
         "- scene.autosave {\"project\":P} guarda si hay cambios pendientes."},
        {"guia.campos-script", "Cambiar un valor expuesto de un script",
         "campo variable propiedad valor velocidad vida script parametro",
         "scene.object.setfield {\"project\":P,\"id\":ID,\"field\":\"nombre\",\"value\":{\"v\":VALOR}} "
         "— VALOR puede ser número, texto, booleano o [x,y,z]."},
        {"guia.archivos", "Trabajar con archivos del proyecto",
         "archivo carpeta src crear renombrar mover listar importar",
         "- Listar: project.src.list {\"project\":P,\"path\":\"Scripts\"}.\n"
         "- Crear archivo: project.src.create {..,\"path\":\"Carpeta/A.txt\",\"file\":true,\"content\":\"...\"}; "
         "carpeta: sin file.\n"
         "- Renombrar/mover: project.src.rename {..,\"path\":\"A\",\"newPath\":\"B\"}.\n"
         "- Importar carpeta externa: project.dir.import {..,\"source\":\"/abs\",\"path\":\"Destino\"}."},
        {"guia.errores", "Ver errores de compilación y diagnósticos",
         "errores fallos compilacion diagnostico warning log que paso",
         "1. script.compile {\"project\":P} (o scene.script.compile para los de la escena).\n"
         "2. diagnostics.list {\"limit\":20} muestra los últimos errores y avisos del motor."},
        {"guia.proyecto-nuevo", "Empezar un proyecto nuevo",
         "proyecto nuevo crear juego empezar 2d 3d",
         "1. project.create {\"name\":\"MiJuego\",\"dimension\":2} (2 o 3).\n"
         "2. project.list para confirmar; los demás comandos usan \"project\":\"MiJuego\"."},
    };
    return guias;
}

}  // namespace funshi::agente::conocimiento
