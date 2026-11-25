/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#ifndef FUNSHI_COMANDOS_BORRADO_H
#define FUNSHI_COMANDOS_BORRADO_H

// Borrado de objetos de la escena, scripts y assets, y listado de assets.
//
// Todo lo borrado va a la papelera del proyecto (ComandosPapelera). Un
// objeto se borra con sus hijos; su copia es un prefab que scene.undo o
// trash.restore vuelven a instanciar. Un script o un asset que algún objeto
// usa se borra igual: los componentes que lo referencian se quitan de esos
// objetos, se informan y trash.restore los vuelve a poner.

#include "Nucleo/Respuesta.h"

#include <nlohmann/json.hpp>

class SceneRegistry;
class GameObject;

namespace funshi {

class ContextoEscena;

ResultadoComando escenaObjetoBorrar(const nlohmann::json& argumentos);   // scene.object.delete
ResultadoComando escenaObjetosBorrar(const nlohmann::json& argumentos);  // scene.objects.delete
ResultadoComando scriptBorrar(const nlohmann::json& argumentos);         // script.delete
ResultadoComando assetListar(const nlohmann::json& argumentos);         // asset.list
ResultadoComando assetBorrar(const nlohmann::json& argumentos);         // asset.delete

// scene.undo de un borrado: instancia el prefab guardado bajo el padre que
// tenía. Devuelve el id nuevo del objeto, o -1 si no se pudo.
int restaurarObjetoBorrado(ContextoEscena& contexto, const nlohmann::json& antes);

// Borra el objeto y sus descendientes, de las hojas hacia arriba.
bool borrarConDescendientes(ContextoEscena& contexto, GameObject* objeto);

// Quita de todos los objetos los componentes que referencian el archivo
// (un texto de su JSON igual a la ruta relativa a src/ o a la absoluta, o
// que termina en "/<ruta relativa>"). Devuelve [{id, name, componentType}].
nlohmann::json quitarReferenciasA(SceneRegistry* registro, const std::string& rutaRelativa,
                                  const std::string& rutaAbsoluta);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_BORRADO_H
