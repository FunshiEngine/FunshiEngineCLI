/*
    FunshiEngineCLI - CLI de automación del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
#ifndef FUNSHI_COMANDOS_ESCENA_H
#define FUNSHI_COMANDOS_ESCENA_H

#include <nlohmann/json.hpp>

#include "Nucleo/Respuesta.h"

namespace funshi {

// scene.list: lista las escenas (archivos .db/.txt) del proyecto.
ResultadoComando escenaListar(const nlohmann::json& argumentos);

// scene.objects: carga la escena y devuelve la jerarquía completa.
ResultadoComando escenaObjetos(const nlohmann::json& argumentos);

// scene.object.get: carga la escena y busca un objeto por id o tag.
ResultadoComando escenaObjetoObtener(const nlohmann::json& argumentos);

// scene.object.getglobalpos: devuelve la posicion global/world de un objeto.
ResultadoComando escenaObjetoPosicionGlobal(const nlohmann::json& argumentos);

// scene.object.settransform: establece el transform local (posicion, rotacion, escala).
ResultadoComando escenaObjetoSetTransform(const nlohmann::json& argumentos);

// scene.object.setcomponent: establece un campo de un componente del objeto.
ResultadoComando escenaObjetoSetComponente(const nlohmann::json& argumentos);

// scene.hierarchy: alias de scene.objects; carga y devuelve la jerarquía.
ResultadoComando escenaJerarquia(const nlohmann::json& argumentos);

// scene.save: guarda la escena actual en disco (serializa el registro completo).
ResultadoComando escenaGuardar(const nlohmann::json& argumentos);

// scene.object.setparent: asigna un objeto como hijo de otro (parentId=0 = root).
ResultadoComando escenaObjetoSetPadre(const nlohmann::json& argumentos);

// scene.object.addcomponent: agrega un componente nuevo al objeto.
// componentType: "Script" | "Model" | "Color" | "AudioSource" | "Collider" | "RigidBody"
ResultadoComando escenaObjetoAddComponente(const nlohmann::json& argumentos);

// scene.object.create: crea un nuevo GameObject en la escena (padre opcional).
ResultadoComando escenaObjetoCrear(const nlohmann::json& argumentos);

// scene.object.clone: clona un objeto existente con sus componentes.
ResultadoComando escenaObjetoClonar(const nlohmann::json& argumentos);

// scene.object.removecomponent: quita un componente de un objeto.
ResultadoComando escenaObjetoQuitarComponente(const nlohmann::json& argumentos);

// scene.object.unparent: desvincula un objeto de su padre (lo vuelve raíz).
ResultadoComando escenaObjetoDesvincularPadre(const nlohmann::json& argumentos);

// scene.object.settag: cambia la etiqueta (tag) de un objeto.
ResultadoComando escenaObjetoSetTag(const nlohmann::json& argumentos);

// scene.object.setactive: activa o desactiva un objeto.
ResultadoComando escenaObjetoSetActivo(const nlohmann::json& argumentos);

// scene.object.components: lista los componentes de un objeto con sus tipos y campos.
ResultadoComando escenaObjetoComponentes(const nlohmann::json& argumentos);

// scene.object.findbytag: busca objetos por etiqueta.
ResultadoComando escenaObjetoBuscarPorTag(const nlohmann::json& argumentos);

// scene.object.findbyname: busca objetos por nombre.
ResultadoComando escenaObjetoBuscarPorNombre(const nlohmann::json& argumentos);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_ESCENA_H
