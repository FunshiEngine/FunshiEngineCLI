/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// RAG de documentos del usuario: la carpeta de documentos (no versionada),
// su base de vectores (AlmacenVectores) y la búsqueda.
//
// actualizar() sincroniza carpeta → base: extrae y embebe solo lo nuevo o
// cambiado (mtime + tamaño), quita lo borrado y guarda. Sin proveedor de
// embeddings guarda los fragmentos sin vector; la búsqueda usa entonces el
// índice léxico (BM25). Si cambia el modelo, se vuelve a embeber todo.
//
// Hilos: actualizar() trabaja sobre una copia y la publica al final, así las
// búsquedas del agente no esperan a la indexación. Una sola actualización a
// la vez.

#include "Agente/Conocimiento/IndiceBm25.h"
#include "Agente/Documentos/AlmacenVectores.h"
#include "Agente/Documentos/Embeddings.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace funshi::agente::documentos {

struct EstadoDocumentos {
    bool actualizando = false;
    std::string fase;               // qué está haciendo (para la TUI)
    int archivos = 0;               // documentos en la base
    int fragmentos = 0;
    int conVector = 0;
    int conError = 0;               // archivos sin texto extraíble
    std::string ultimoError;
    std::string modelo;             // modelo de embeddings de la base
    std::int64_t actualizado = 0;   // segundos desde epoch de la última actualización
};

struct ResultadoDocumento {
    std::string ruta;
    int pagina = 0;
    std::string texto;
    double puntaje = 0.0;
    bool vectorial = false;         // false = búsqueda léxica
};

class BaseDocumentos {
public:
    BaseDocumentos(std::filesystem::path carpeta, std::filesystem::path archivoBase);

    // La de la configuración actual (<config>/documentos y documentos.fvdb).
    static BaseDocumentos& global();

    const std::filesystem::path& carpeta() const { return carpeta_; }
    // Crea la carpeta con un LEEME.txt que explica para qué es.
    void prepararCarpeta() const;
    // Archivos de la carpeta con texto extraíble (recursivo).
    std::vector<std::filesystem::path> archivos() const;

    using Progreso = std::function<void(const EstadoDocumentos&)>;
    // false si ya había una actualización en curso.
    bool actualizar(ProveedorEmbeddings* proveedor, const Progreso& progreso = nullptr);

    std::vector<ResultadoDocumento> buscar(const std::string& consulta, std::size_t k,
                                           ProveedorEmbeddings* proveedor) const;
    // Bloque de contexto para un pedido: los fragmentos que superan el umbral.
    std::string contextoParaPedido(const std::string& pedido, ProveedorEmbeddings* proveedor,
                                   std::vector<std::string>* usados = nullptr) const;

    EstadoDocumentos estado() const;

    // Fragmentos de ~`maximo` caracteres por párrafos, con `solape`
    // caracteres del anterior; '\f' separa páginas.
    static std::vector<Fragmento> fragmentar(const std::string& texto, std::size_t maximo = 900,
                                             std::size_t solape = 150);

    // Similitud coseno para el contexto automático. Con Qwen3-Embedding-0.6B
    // los fragmentos con la respuesta quedan en 0.45–0.7 y lo no
    // relacionado por debajo de 0.37.
    static constexpr double kUmbralVector = 0.40;
    static constexpr double kUmbralLexico = 4.0;    // puntaje BM25 para el contexto automático

private:
    void cargarSiHaceFalta() const;
    void reconstruirLexico();
    void fijarEstado(const std::function<void(EstadoDocumentos&)>& cambio, const Progreso& progreso);

    std::filesystem::path carpeta_;
    std::filesystem::path archivoBase_;
    mutable std::mutex mutex_;
    mutable bool cargado_ = false;
    mutable AlmacenVectores almacen_;
    conocimiento::IndiceBm25 lexico_;
    std::vector<std::pair<std::size_t, std::size_t>> mapaLexico_;  // índice → (documento, fragmento)
    EstadoDocumentos estado_;
    std::atomic<bool> enCurso_{false};
};

}  // namespace funshi::agente::documentos
