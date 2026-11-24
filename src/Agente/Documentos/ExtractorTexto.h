/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Extracción de texto de los documentos que el usuario deja para el agente.
//
//   texto y código   .txt .md .csv .log, y todo lenguaje de la tabla de
//                    Lenguajes (se descartan binarios; Latin-1 → UTF-8)
//   HTML / XML       sin etiquetas, sin <script>/<style>, entidades resueltas
//   PDF              pdftotext (poppler) si está instalado; si no, extractor
//                    propio: streams FlateDecode + operadores Tj/TJ. Cada
//                    página termina en '\f' (para saber de qué página es un
//                    fragmento)
//   DOCX PPTX XLSX   ZIP (miniz) + XML del contenido
//   ODT ODP ODS EPUB

#include <filesystem>
#include <string>

namespace funshi::agente::documentos {

struct TextoExtraido {
    bool ok = false;
    std::string texto;
    std::string metodo;  // "texto", "html", "pdftotext", "pdf-interno", "docx"…
    std::string error;
};

// ¿Hay un extractor para la extensión del archivo?
bool esExtraible(const std::filesystem::path& archivo);

TextoExtraido extraerTexto(const std::filesystem::path& archivo);

// Piezas expuestas para probarlas sin archivos.
std::string xmlATexto(const std::string& xml);
std::string textoDePdf(const std::string& bytes);  // extractor propio
std::string aUtf8(const std::string& bytes);       // UTF-8 válido o Latin-1 convertido

}  // namespace funshi::agente::documentos
