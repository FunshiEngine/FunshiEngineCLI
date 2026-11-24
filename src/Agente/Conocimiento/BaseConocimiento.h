/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Base de conocimiento del agente (RAG): qué comandos existen y cómo usarlos.
//
// Fuentes:
//   - comandos: la ayuda de cada comando del catálogo (Nucleo/AyudaComandos),
//     con su ejemplo convertido a la llamada JSON de `ejecutar_comando`;
//   - guías: recetas de varios pasos (GuiasAgente.cpp);
//   - documentos del usuario: `<carpeta de config>/conocimiento/*.md`,
//     partidos por encabezados `##`.
//
// Silvy inyecta los documentos más relevantes antes de cada pedido y el
// modelo puede buscar más con la herramienta `buscar_comandos`.

#include "Agente/Conocimiento/IndiceBm25.h"

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace funshi::agente::conocimiento {

struct Documento {
    enum class Tipo { Comando, Guia, Usuario };
    Tipo tipo = Tipo::Comando;
    std::string id;         // nombre del comando, o identificador de la guía
    std::string titulo;
    std::string contenido;  // texto que ve el modelo
};

struct Guia {
    const char* id;
    const char* titulo;
    const char* palabrasClave;  // se indexan, no se muestran
    const char* pasos;
};

// Guías incluidas en la CLI (GuiasAgente.cpp).
const std::vector<Guia>& guiasIncluidas();

struct ResultadoBusqueda {
    const Documento* documento = nullptr;
    double puntaje = 0.0;
};

class BaseConocimiento {
public:
    // `dirUsuario` vacío: sin documentos del usuario.
    explicit BaseConocimiento(const std::filesystem::path& dirUsuario = {});

    // Instancia compartida: comandos + guías + `<config>/conocimiento/`.
    static std::shared_ptr<const BaseConocimiento> global();

    std::vector<ResultadoBusqueda> buscar(const std::string& consulta, std::size_t k,
                                          double puntajeMinimo = 0.0) const;

    // Documento de un comando por su nombre exacto; nullptr si no existe.
    const Documento* comando(const std::string& nombre) const;

    // Bloque de contexto para un pedido del usuario: los documentos más
    // relevantes, compactos. Vacío si nada supera el umbral.
    // `herramientas`: tools disponibles, para indicar si un comando tiene la
    // suya o se usa vía ejecutar_comando.
    // `usados`: si no es nulo, recibe los ids de los documentos incluidos.
    std::string contextoParaPedido(const std::string& pedido,
                                   const std::set<std::string>& herramientas,
                                   std::size_t k = 4,
                                   std::vector<std::string>* usados = nullptr) const;

    // "Uso correcto de <comando>": argumentos y ejemplo listo para usar.
    // Vacío si el comando no existe.
    static std::string usoDeComando(const std::string& comando);

    // Texto de un documento para devolverlo como resultado de herramienta.
    static std::string formatear(const Documento& d, const std::set<std::string>& herramientas);

    std::size_t tamano() const { return documentos_.size(); }

private:
    void agregar(Documento d, const std::string& textoIndexado);
    void cargarDocumentosUsuario(const std::filesystem::path& dir);

    std::vector<Documento> documentos_;
    IndiceBm25 indice_;
};

}  // namespace funshi::agente::conocimiento
