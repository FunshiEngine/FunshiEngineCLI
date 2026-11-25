/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Nucleo/AyudaComandos.h"

#include "Nucleo/CatalogoComandos.h"

namespace funshi {

namespace {

const ArgumentoAyuda kArgId{"id", "entero", true, "Id del objeto en la escena"};

std::vector<AyudaComando> construirAyuda() {
    std::vector<AyudaComando> ayuda = {
        {"help", "Lista todos los comandos disponibles.", false, {}, "help"},
        {"version", "Muestra la versión de la CLI.", false, {}, "version"},
        {"project.create", "Crea un proyecto nuevo.", false,
         {{"name", "texto", true, "Nombre del proyecto"},
          {"dimension", "2 | 3 | \"2d\" | \"3d\"", false, "Dimensión (por defecto 3)"}},
         "project.create name=MiJuego dimension=2"},
        {"project.list", "Lista los proyectos con su dimensión y validez.", false, {},
         "project.list"},
        {"project.config.get",
         "Muestra la configuración del proyecto (dimensión, versión, ventanas, cámara).", true,
         {}, "project.config.get"},
        {"project.src.create", "Crea un archivo o carpeta dentro de src/ del proyecto.", true,
         {{"path", "texto", true, "Ruta dentro de src/, sin \"src/\" delante; un archivo lleva su extensión (Scripts/Jugador.java)"},
          {"file", "true | false", false, "true crea archivo; false carpeta (por defecto)"},
          {"content", "texto", false, "Contenido inicial del archivo"}},
         "project.src.create path=Scripts/Enemigo.cpp file=true content=\"// enemigo\""},
        {"project.src.list", "Lista archivos y carpetas de src/ del proyecto.", true,
         {{"path", "texto", false, "Subcarpeta de src/ a listar, sin \"src/\" delante"}},
         "project.src.list path=Scripts"},
        {"project.src.delete", "Borra un archivo o carpeta de src/.", true,
         {{"path", "texto", true, "Ruta dentro de src/, sin \"src/\" delante"}},
         "project.src.delete path=Scripts/Viejo.cpp"},
        {"project.src.rename", "Renombra o mueve un archivo/carpeta dentro de src/.", true,
         {{"path", "texto", true, "Ruta actual"},
          {"newPath", "texto", true, "Ruta nueva"}},
         "project.src.rename path=Scripts/A.cpp newPath=Scripts/B.cpp"},
        {"project.dir.import", "Importa una carpeta completa (recursiva) a src/.", true,
         {{"source", "texto", true, "Ruta absoluta de la carpeta origen"},
          {"path", "texto", true, "Ruta destino dentro de src/, sin \"src/\" delante"},
          {"overwrite", "true | false", false, "Sobrescribir archivos existentes"}},
         "project.dir.import source=/home/usuario/Sprites path=Assets/Sprites"},
        {"project.fs.create", "Crea una carpeta en el vfs fs/ del proyecto.", true,
         {{"name", "texto", true, "Nombre de la carpeta"}},
         "project.fs.create name=Niveles"},
        {"file.create", "Crea un archivo en el vfs fs/ del proyecto.", true,
         {{"name", "texto", true, "Nombre del archivo"},
          {"path", "texto", false, "Subcarpeta dentro de fs/"},
          {"content", "texto", false, "Contenido inicial"}},
         "file.create name=nivel1.txt path=Niveles content=\"hola\""},
        {"scene.list", "Lista las escenas del proyecto.", true, {}, "scene.list"},
        {"scene.objects", "Devuelve todos los objetos de la escena con sus componentes.", true,
         {}, "scene.objects"},
        {"scene.object.get", "Obtiene un objeto por id o por tag.", true,
         {{"id", "entero", false, "Id del objeto (o usa tag)"},
          {"tag", "texto", false, "Tag del objeto (o usa id)"}},
         "scene.object.get id=6"},
        {"scene.object.getglobalpos", "Posición global de un objeto (por id o tag).", true,
         {{"id", "entero", false, "Id del objeto (o usa tag)"},
          {"tag", "texto", false, "Tag del objeto (o usa id)"}},
         "scene.object.getglobalpos id=6"},
        {"scene.object.settransform", "Cambia posición, rotación y/o escala local.", true,
         {{"id", "entero", false, "Id del objeto (o usa tag)"},
          {"tag", "texto", false, "Tag del objeto (o usa id)"},
          {"position", "[x,y,z]", false, "Posición local"},
          {"rotation", "[x,y,z]", false, "Rotación local"},
          {"scale", "[x,y,z]", false, "Escala local"}},
         "scene.object.settransform id=6 position=[0,1.5,0] scale=[2,2,2]"},
        {"scene.object.setcomponent", "Modifica un campo de un componente.", true,
         {{"id", "entero", false, "Id del objeto (o usa tag)"},
          {"tag", "texto", false, "Tag del objeto (o usa id)"},
          {"componentType", "Transform | Model | Script", true, "Tipo de componente"},
          {"field", "texto", true,
           "Transform: position/rotation/scale · Model: modelPath · Script: scriptPath"},
          {"value", "[x,y,z] | texto", true, "Nuevo valor"}},
         "scene.object.setcomponent id=6 componentType=Transform field=position value=[1,0,0]"},
        {"scene.hierarchy", "Jerarquía completa de objetos (igual que scene.objects).", true,
         {}, "scene.hierarchy"},
        {"scene.save", "Guarda la escena del proyecto.", true, {}, "scene.save"},
        {"scene.object.setparent", "Cambia el padre de un objeto.", true,
         {kArgId, {"parentId", "entero", true, "Id del nuevo padre"}},
         "scene.object.setparent id=6 parentId=2"},
        {"scene.object.addcomponent", "Agrega un componente a un objeto.", true,
         {kArgId,
          {"componentType", "Script | Model | Color | AudioSource | Collider | RigidBody", true,
           "Tipo de componente"},
          {"value", "texto | [r,g,b,a] | {...}", false,
           "Script/Model: ruta · Color: [r,g,b,a] · AudioSource: clip · Collider: {\"tipo\":..,\"radio\":..}"},
          {"volume", "número", false, "AudioSource: volumen"},
          {"loop", "true | false", false, "AudioSource: repetir"},
          {"autoplay", "true | false", false, "AudioSource: reproducir al iniciar"},
          {"espacial", "true | false", false, "AudioSource: audio 3D"},
          {"masa", "número", false, "RigidBody: masa (por defecto 1)"}},
         "scene.object.addcomponent id=6 componentType=Collider value={\"tipo\":\"esfera\",\"radio\":0.5}"},
        {"scene.object.create", "Crea un objeto vacío en la escena.", true,
         {{"name", "texto", true, "Nombre del objeto"},
          {"parentId", "entero", false, "Id del padre (por defecto la raíz)"}},
         "scene.object.create name=Enemigo parentId=0"},
        {"scene.object.delete", "Borra o elimina un objeto de la escena con sus hijos (va a la papelera; scene.undo o trash.restore lo recuperan).", true,
         {kArgId}, "scene.object.delete id=6"},
        {"scene.object.clone", "Clona un objeto (con sus hijos).", true,
         {{"id", "entero", true, "Id del objeto a clonar"},
          {"parentId", "entero", false, "Padre del clon"}},
         "scene.object.clone id=6 parentId=0"},
        {"scene.object.removecomponent", "Quita un componente de un objeto.", true,
         {kArgId, {"componentType", "texto", true, "Tipo de componente a quitar"}},
         "scene.object.removecomponent id=6 componentType=Collider"},
        {"scene.object.unparent", "Desvincula un objeto de su padre (pasa a la raíz).", true,
         {kArgId}, "scene.object.unparent id=6"},
        {"scene.object.settag", "Cambia el tag de un objeto.", true,
         {kArgId, {"tag", "texto", true, "Nuevo tag"}},
         "scene.object.settag id=6 tag=Enemigo"},
        {"scene.object.setactive", "Activa o desactiva un objeto.", true,
         {kArgId, {"active", "true | false", true, "Estado activo"}},
         "scene.object.setactive id=6 active=false"},
        {"scene.object.components", "Lista los componentes de un objeto.", true, {kArgId},
         "scene.object.components id=6"},
        {"scene.object.findbytag", "Busca objetos por tag.", true,
         {{"tag", "texto", true, "Tag a buscar"}}, "scene.object.findbytag tag=Enemigo"},
        {"scene.object.findbyname", "Busca objetos por nombre.", true,
         {{"name", "texto", true, "Nombre a buscar"}},
         "scene.object.findbyname name=Enemigo"},
        {"scene.object.getglobaltransform", "Transform global (posición/rotación/escala).", true,
         {kArgId}, "scene.object.getglobaltransform id=6"},
        {"asset.import", "Importa un modelo, textura o audio al proyecto.", true,
         {{"source", "texto", true, "Ruta absoluta del archivo"}},
         "asset.import source=/tmp/modelo.fbx"},
        {"asset.list", "Lista los assets del proyecto (modelos, imágenes, audio, prefabs, tilesets, partículas, interfaces) con su tipo.", true,
         {{"type", "texto", false, "Filtra por tipo: modelo, imagen, audio, prefab, tileset, particulas, interfaz"}},
         "asset.list type=audio"},
        {"asset.delete", "Borra o elimina un asset (sonido, modelo, imagen, prefab…) de src/ y lo quita de quien lo usaba (no scripts: script.delete).", true,
         {{"path", "texto", true, "Ruta del asset dentro de src/, como la devuelve asset.list (Sonidos/salto.wav)"}},
         "asset.delete path=Sonidos/salto.wav"},
        {"trash.list", "Muestra qué hay en la papelera del proyecto: lo borrado (archivos, assets, scripts y objetos) que se puede recuperar.", true,
         {}, "trash.list"},
        {"trash.restore", "Recupera o restaura algo borrado (script, sonido, modelo, imagen, objeto…) desde la papelera a su lugar y lo vuelve a poner en los objetos que lo usaban.", true,
         {{"id", "entero", true, "Id de la entrada, como la da trash.list"},
          {"overwrite", "true | false", false, "Reemplazar si ya existe un archivo con esa ruta"}},
         "trash.restore id=3"},
        {"trash.empty", "Vacía la papelera (o solo una entrada): lo borrado ya no se puede recuperar.", true,
         {{"id", "entero", false, "Solo esa entrada; sin id, toda la papelera"}},
         "trash.empty id=3"},
        {"asset.validar", "Indica si la extensión de un archivo es un asset soportado.", false,
         {{"path", "texto", true, "Nombre o ruta del archivo"}},
         "asset.validar path=modelo.fbx"},
        {"asset.extensions", "Lista las extensiones de assets soportadas.", false, {},
         "asset.extensions"},
        {"script.list", "Lista los scripts (.cpp/.java) del proyecto.", true, {},
         "script.list"},
        {"script.delete", "Borra un archivo de código .java o .cpp de src/ y lo quita de quien lo usaba.", true,
         {{"path", "texto", true, "Ruta dentro de src/, sin \"src/\" delante (Scripts/Enemigo.cpp)"}},
         "script.delete path=Scripts/Enemigo.cpp"},
        {"script.compile", "Compila todos los scripts del proyecto.", true, {},
         "script.compile"},
        {"scene.script.compile", "Compila los scripts de la escena (o los de un objeto).", true,
         {{"id", "entero", false, "Solo el script de este objeto"}},
         "scene.script.compile id=6"},
        {"scene.script.reload", "Recarga el script de un objeto.", true, {kArgId},
         "scene.script.reload id=6"},
        {"scene.undo", "Deshace el último cambio en la escena.", true, {}, "scene.undo"},
        {"scene.redo", "Rehace el último cambio deshecho.", true, {}, "scene.redo"},
        {"scene.autosave", "Guarda la escena si tiene cambios pendientes.", true, {},
         "scene.autosave"},
        {"scene.object.setfield", "Cambia el valor de una variable expuesta del Script de un objeto (no escribe código: el código va en project.src.create).", true,
         {kArgId,
          {"field", "texto", true, "Nombre del campo"},
          {"value", "{\"v\":valor}", true, "Objeto con el valor"}},
         "scene.object.setfield id=6 field=vida value={\"v\":100}"},
        {"diagnostics.list", "Muestra los últimos eventos de diagnóstico.", false,
         {{"limit", "entero", false, "Máximo de eventos (por defecto 100, tope 1000)"}},
         "diagnostics.list limit=20"},
        {"diagnostics.clear", "Borra el registro de diagnósticos.", false, {},
         "diagnostics.clear"},
        {"cli.config.get", "Muestra las rutas, el contexto y la configuración LLM (claves enmascaradas).", false, {},
         "cli.config.get"},
        {"cli.config.set",
         "Guarda rutas, contexto o configuración del modelo remoto (\"\" vuelve al valor por defecto en rutas).", false,
         {{"projectsDataDir", "texto", false, "Datos/proyectos del motor"},
          {"localInferenceDir", "texto", false, "Base de inferencia local"},
          {"modelsDir", "texto", false, "Modelos GGUF"},
          {"hfCacheDir", "texto", false, "Cache de Hugging Face"},
          {"llamaServerPath", "texto", false,
           "Binario llama-server propio (\"\" = el oficial, que se descarga solo)"},
          {"contextSize", "entero", false, "Contexto del modelo local en tokens (por defecto 32768)"},
          {"provider", "google | nvidia | openai | custom | local", false, "Proveedor/preset LLM remoto"},
          {"remoteBaseUrl", "texto", false, "URL base OpenAI-compatible (p. ej. https://integrate.api.nvidia.com/v1)"},
          {"modelName", "texto", false, "Nombre/id del modelo remoto"},
          {"apiKey", "texto", false, "API key genérica/remota"},
          {"googleApiKey", "texto", false, "API key de Google Gemini"},
          {"nvidiaApiKey", "texto", false, "API key de NVIDIA NIM"},
          {"thinking", "off | low | medium | high | xhigh", false, "Nivel de razonamiento (si el proveedor lo soporta)"},
          {"power", "low | medium | high | xhigh", false, "Calidad/presupuesto de respuesta"}},
         "cli.config.set provider=\"nvidia\" nvidiaApiKey=\"...\" modelName=\"meta/llama-3.1-8b-instruct\""},
        {"cli.models.list",
         "Lista modelos disponibles consultando el endpoint OpenAI-compatible configurado.", false,
         {{"provider", "google | nvidia | openai | custom | local", false, "Proveedor a consultar"},
          {"remoteBaseUrl", "texto", false, "URL base a consultar (sobrescribe provider)"},
          {"apiKey", "texto", false, "API key genérica (opcional si ya está configurada)"},
          {"googleApiKey", "texto", false, "API key de Google (opcional si ya está configurada)"},
          {"nvidiaApiKey", "texto", false, "API key de NVIDIA (opcional si ya está configurada)"}},
         "cli.models.list provider=\"nvidia\""},
        {"agente.procesar", "Envía un texto a Silvy y devuelve su respuesta.", false,
         {{"text", "texto", true, "Orden en lenguaje natural"}},
         "agente.procesar text=\"listar proyectos\""},
    };

    // Comandos del catálogo sin documentación: se listan igual.
    for (const ComandoCLI comando : todosLosComandos()) {
        const std::string texto = textoDesdeComando(comando);
        bool encontrado = false;
        for (const auto& a : ayuda) {
            if (a.comando == texto) { encontrado = true; break; }
        }
        if (!encontrado) {
            ayuda.push_back({texto, "(sin documentación)", false, {}, texto});
        }
    }
    return ayuda;
}

}  // namespace

const std::vector<AyudaComando>& ayudaComandos() {
    static const std::vector<AyudaComando> ayuda = construirAyuda();
    return ayuda;
}

const AyudaComando* buscarAyudaComando(const std::string& comando) {
    for (const auto& a : ayudaComandos()) {
        if (a.comando == comando) return &a;
    }
    return nullptr;
}

}  // namespace funshi
