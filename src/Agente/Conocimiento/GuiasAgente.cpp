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
         "1. project.src.create {\"project\":P,\"path\":\"Scripts/Nombre.java\",\"file\":true,\"content\":CODIGO} "
         "con el código completo según la guía «Scripts del motor» (Java: public class Nombre implements "
         "Comportamiento con iniciar/actualizar; C++: IScriptBehaviour y la fábrica FUNSHI_CREAR_COMPORTAMIENTO).\n"
         "2. script.compile {\"project\":P} y revisa diagnostics.list si falla.\n"
         "3. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"Script\",\"value\":RUTA} "
         "usando la ruta tal como la devuelve script.list.\n"
         "4. scene.script.reload {\"project\":P,\"id\":ID}."},
        {"guia.borrar", "Borrar o eliminar un objeto, un script o un asset",
         "borrar borra borralo bórralo eliminar elimina eliminalo elimínalo quitar quita sacar saca destruir destruye "
         "objeto objetos todos vaciar limpiar escena script asset sonido modelo textura imagen archivo papelera recuperar recupera restaurar restaura "
         "deshacer borré borrado borrada",
         "El usuario confirma cada borrado: si no lo autoriza, no insistas.\n"
         "- Objeto (con sus hijos): si no sabes el id, scene.object.findbyname {\"project\":P,\"name\":\"X\"}; "
         "luego scene.object.delete {\"project\":P,\"id\":ID}. scene.undo lo restaura.\n"
         "- Varios objetos o todos: usa scene.objects.delete (una sola confirmación). Todos: {\"project\":P,\"all\":true}. Grupo: {\"project\":P,\"ids\":[ID1,ID2]}. Borra también hijos; ids ya borrados como hijos se omiten.\n"
         "- Script: script.list {\"project\":P} y script.delete {\"project\":P,\"path\":RUTA} (quita el "
         "componente Script de los objetos que lo usan).\n"
         "- Asset (modelo, imagen, audio, prefab, tileset, partículas, interfaz): asset.list {\"project\":P} y "
         "asset.delete {\"project\":P,\"path\":RUTA} (quita sus componentes de los objetos que lo usan).\n"
         "- Otro archivo o una carpeta de src/: project.src.delete {\"project\":P,\"path\":RUTA}.\n"
         "Todo va a la papelera: trash.list {\"project\":P} la muestra y trash.restore {\"project\":P,\"id\":N} "
         "lo recupera (con sus referencias en los objetos)."},
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
        {"guia.tilemap-2d", "Crear un tilemap 2D (tileset, tilemap y colisión)",
         "tilemap tileset 2d sprite grilla mapa nivel plataforma colision tile",
         "1. Importa la imagen del atlas: asset.import {\"project\":P,\"source\":\"/ruta/atlas.png\"} → PATH_IMAGEN.\n"
         "2. Crea el tileset JSON en src/ (formato compatible con Tiled, subset): project.src.create {\"project\":P,\"path\":\"Tiles/mi_tileset.json\",\"file\":true,\"content\":JSON}. Ejemplo mínimo:\n"
         "{\n  \"image\": \"../Assets/Images/atlas.png\",\n  \"imagewidth\": 256, \"imageheight\": 256,\n  \"tilewidth\": 32, \"tileheight\": 32,\n  \"columns\": 8, \"tilecount\": 64,\n  \"margin\": 0, \"spacing\": 0,\n  \"tiles\": [{\"id\": 1, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]}]\n}\n"
         "La ruta \"image\" es relativa al archivo del tileset. Ajusta medidas/tilecount a tu atlas. Marca tiles sólidos con la propiedad solid=true.\n"
         "3. Crea el objeto: scene.object.create {\"project\":P,\"name\":\"Tilemap\"} → ID.\n"
         "4. Agrega el componente Tilemap: scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"Tilemap\",\"value\":\"Tiles/mi_tileset.json\",\"layer\":{\"name\":\"Suelo\",\"width\":20,\"height\":10,\"collision\":true}}.\n"
         "5. (Opcional) Colisión física del tilemap: scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"TilemapCollider\"}. Requiere que la capa tenga collision=true y tiles marcados solid.\n"
         "6. Para sprites 2D sueltos: importa imagen y agrega SpriteRenderer: scene.object.addcomponent {\"project\":P,\"id\":ID_OBJ,\"componentType\":\"SpriteRenderer\",\"value\":\"Assets/Images/player.png\",\"size\":[1,1]}.\n"
         "7. Guarda: scene.save {\"project\":P}. Para revisar archivos usa project.src.read {\"project\":P,\"path\":\"Tiles/mi_tileset.json\"}."},
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
         "archivo carpeta src crear renombrar mover listar importar leer script tilemap json",
         "- Listar: project.src.list {\"project\":P,\"path\":\"Scripts\"}.\n"
         "- Leer archivo de texto (script, tilemap .json, tileset .json, etc.): project.src.read {\"project\":P,\"path\":\"Scripts/A.java\"}.\n"
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
