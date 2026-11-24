/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Base de datos local de los documentos del usuario: fragmentos de texto con
// su vector de embeddings, en un archivo binario propio (formato FVDB v1,
// little-endian):
//
//   "FVDB" u32 versión
//   str modelo · u32 dimensión · u32 documentos
//   por documento: str ruta (relativa a la carpeta) · i64 mtime · u64 tamaño
//                  str método de extracción · str error · u32 fragmentos
//     por fragmento: str texto · u32 página · u8 tieneVector · f32[dim]
//   (str = u32 largo + bytes UTF-8)
//
// Los vectores se guardan normalizados: la similitud coseno es el producto
// punto. La búsqueda es exhaustiva (miles de fragmentos: milisegundos).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace funshi::agente::documentos {

struct Fragmento {
    std::string texto;
    int pagina = 0;              // 1, 2, … (0 = sin páginas)
    std::vector<float> vector;   // vacío si no se pudo embeber
};

struct DocumentoIndexado {
    std::string ruta;            // relativa a la carpeta de documentos
    std::int64_t mtime = 0;
    std::uint64_t tamano = 0;
    std::string metodo;          // cómo se extrajo el texto
    std::string error;           // por qué no tiene fragmentos
    std::vector<Fragmento> fragmentos;
};

struct Coincidencia {
    const DocumentoIndexado* documento = nullptr;
    const Fragmento* fragmento = nullptr;
    double puntaje = 0.0;
};

class AlmacenVectores {
public:
    std::string modelo;   // modelo de embeddings con el que se calcularon los vectores
    int dimension = 0;
    std::vector<DocumentoIndexado> documentos;

    bool cargar(const std::filesystem::path& archivo, std::string* error = nullptr);
    bool guardar(const std::filesystem::path& archivo) const;  // atómico (.tmp + rename)

    DocumentoIndexado* buscarDocumento(const std::string& ruta);
    std::size_t totalFragmentos() const;
    std::size_t fragmentosConVector() const;

    // Los `k` fragmentos más parecidos a `consulta` (vector normalizado).
    std::vector<Coincidencia> masParecidos(const std::vector<float>& consulta, std::size_t k) const;
};

// Normaliza a largo 1 (no hace nada con el vector nulo).
void normalizar(std::vector<float>& v);

}  // namespace funshi::agente::documentos
