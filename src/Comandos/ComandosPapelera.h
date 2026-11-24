/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#ifndef FUNSHI_COMANDOS_PAPELERA_H
#define FUNSHI_COMANDOS_PAPELERA_H

// Papelera del proyecto (<proyecto>/Memory/papelera, fuera de src/: el motor
// no carga lo que hay ahí). Lo que se borra se mueve ahí y se anota en
// indice.json con qué era, de dónde salió, cuándo se borró y qué
// referencias se quitaron de los objetos:
//   - archivos y carpetas de src/ (script.delete, asset.delete,
//     project.src.delete), guardados en <papelera>/<id>/;
//   - objetos de la escena (scene.object.delete), como el prefab que guarda
//     el borrado.
// trash.restore lo devuelve a su lugar (y vuelve a poner en los objetos los
// componentes que se le quitaron); trash.empty lo borra para siempre.

#include "Nucleo/Respuesta.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace funshi {

std::filesystem::path carpetaPapelera(const std::string& proyecto);

// Mueve un archivo o carpeta de src/ a la papelera y lo anota. Devuelve el
// id de la entrada, o -1 con `error`.
int moverAPapelera(const std::string& proyecto, const std::string& rutaRelativa, const std::filesystem::path& rutaAbsoluta,
                   const std::string& tipo, const nlohmann::json& referencias, std::string& error);
// Anota un objeto borrado cuya copia (prefab) ya está en la papelera.
int anotarObjetoEnPapelera(const std::string& proyecto, const std::string& nombre, int idObjeto, int padreId,
                           const std::string& prefab);
// Saca de la papelera la entrada de ese prefab (el objeto volvió por scene.undo).
void quitarDePapeleraPorPrefab(const std::string& proyecto, const std::string& prefab);

ResultadoComando papeleraListar(const nlohmann::json& argumentos);    // trash.list
ResultadoComando papeleraRestaurar(const nlohmann::json& argumentos); // trash.restore
ResultadoComando papeleraVaciar(const nlohmann::json& argumentos);    // trash.empty

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_PAPELERA_H
