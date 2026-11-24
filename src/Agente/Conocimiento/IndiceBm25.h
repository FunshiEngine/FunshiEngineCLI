/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Índice de búsqueda léxica BM25 para la base de conocimiento del agente.
//
// Se eligió BM25 en lugar de embeddings: el corpus es chico (decenas de
// documentos), el modelo local es de chat (sus embeddings no son buenos para
// recuperar) y así la búsqueda no depende de un servidor ni de red. El
// vocabulario en español se cubre con plegado de acentos, singularización
// simple y una tabla de sinónimos que se aplica a la consulta.

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace funshi::agente::conocimiento {

class IndiceBm25 {
public:
    // Agrega un documento; devuelve su índice. `texto` es lo que se indexa.
    std::size_t agregar(const std::string& texto);

    // Prepara las estadísticas (idf). Llamar después de agregar todo.
    void construir();

    // Los `k` documentos con mayor puntaje para la consulta (ya expandida
    // con sinónimos). Puntaje 0 = sin términos en común; se omiten.
    std::vector<std::pair<std::size_t, double>> buscar(const std::string& consulta,
                                                       std::size_t k) const;

    std::size_t tamano() const { return documentos_.size(); }

    // Minúsculas, sin acentos, sin palabras vacías, separado en palabras y
    // con plural simple quitado. "scene.object.settransform" aporta además
    // "scene", "object", "settransform" y "transform".
    static std::vector<std::string> tokenizar(const std::string& texto);

    // Tokens de la consulta más sus sinónimos del vocabulario de comandos.
    static std::vector<std::string> expandirConsulta(const std::string& consulta);

private:
    struct Documento {
        std::unordered_map<std::string, int> frecuencias;
        std::size_t largo = 0;
    };
    std::vector<Documento> documentos_;
    std::unordered_map<std::string, double> idf_;
    double largoPromedio_ = 0.0;
};

}  // namespace funshi::agente::conocimiento
