/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — documentos del usuario para el RAG.
//
// Al abrir la TUI, un hilo de fondo prepara la carpeta y, si tiene archivos,
// levanta el servidor de embeddings (descargando el modelo la primera vez) e
// indexa lo nuevo o cambiado. El avance llega a la barra de estado.
//   /docs              estado, carpeta y archivos indexados
//   /docs actualizar   vuelve a indexar ahora
//   /docs buscar texto busca en los documentos (lo mismo que buscar_documentos)
#include "Tui/AplicacionTui.h"

#include "Agente/Documentos/BaseDocumentos.h"
#include "Agente/Documentos/Embeddings.h"
#include "Agente/LLM/Config/LlmConfig.h"

#include <ctime>
#include <thread>

namespace funshi {

namespace docs = agente::documentos;

namespace {

std::string hora(std::int64_t segundos) {
    if (segundos <= 0) return "nunca";
    const std::time_t t = static_cast<std::time_t>(segundos);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m %H:%M", std::localtime(&t));
    return buf;
}

std::string resumenEstado(const docs::EstadoDocumentos& e) {
    if (e.actualizando) return "indexando: " + e.fase;
    std::string s = std::to_string(e.archivos) + " archivo(s) · " + std::to_string(e.fragmentos) + " fragmentos";
    if (e.fragmentos > 0) {
        s += e.conVector == e.fragmentos ? " · búsqueda semántica" : e.conVector == 0 ? " · solo por palabras"
                                                                                      : " · " + std::to_string(e.conVector) + " con embeddings";
    }
    if (e.conError > 0) s += " · " + std::to_string(e.conError) + " sin texto";
    return s + " · actualizado " + hora(e.actualizado);
}

}  // namespace

void AplicacionTui::iniciarDocumentos(bool aPedido) {
    const auto compartido = compartido_;
    std::thread([compartido, aPedido]() {
        const auto avisar = [&](const std::string& texto, bool error) {
            std::lock_guard<std::mutex> bloqueo(compartido->mutex);
            compartido->avisoDocumentos = texto;
            compartido->avisoDocumentosError = error;
            compartido->avisoDocumentosPendiente = true;
            if (compartido->pantalla) compartido->pantalla->PostEvent(ftxui::Event::Custom);
        };
        auto& base = docs::BaseDocumentos::global();
        base.prepararCarpeta();
        if (base.archivos().empty()) {
            if (aPedido) avisar("Documentos: la carpeta está vacía (" + base.carpeta().string() + ")", false);
            return;
        }
        const auto config = agente::llm::LlmConfig::load();
        std::string error;
        auto proveedor = docs::ServidorEmbeddings::global().asegurar(
            config, [&](const std::string& paso) { avisar("Documentos: " + paso, false); }, error);
        if (!proveedor) avisar("Documentos: sin embeddings (" + error + "); se indexa para búsqueda por palabras", true);
        const bool hecho = base.actualizar(proveedor.get(), [&](const docs::EstadoDocumentos& e) {
            if (e.actualizando) avisar("Documentos: " + e.fase, false);
        });
        if (!hecho) {
            avisar("Documentos: ya hay una indexación en curso", false);
            return;
        }
        const auto e = base.estado();
        avisar("Documentos: " + resumenEstado(e) + (e.ultimoError.empty() ? "" : " · " + e.ultimoError),
               !e.ultimoError.empty());
    }).detach();
}

void AplicacionTui::aplicarAvisoDocumentos() {
    std::lock_guard<std::mutex> bloqueo(compartido_->mutex);
    if (!compartido_->avisoDocumentosPendiente) return;
    compartido_->avisoDocumentosPendiente = false;
    mensaje_ = compartido_->avisoDocumentos;
    mensajeEsError_ = compartido_->avisoDocumentosError;
    if (seccionActual() == SeccionTui::Configuracion) recargarSeccionActual(false);
}

bool AplicacionTui::ordenDeDocumentos(const std::string& orden, const std::string& resto) {
    if (orden != "docs") return false;
    auto sesion = sesionActual();
    if (!sesion) return true;
    auto& base = docs::BaseDocumentos::global();
    if (resto == "actualizar") {
        iniciarDocumentos(true);
        agregarMensajeChat(*sesion, "Indexando los documentos de " + base.carpeta().string() + "…", MensajeChat::Rol::Estado);
    } else if (resto.rfind("buscar ", 0) == 0) {
        const std::string consulta = resto.substr(7);
        const auto proveedor = docs::ServidorEmbeddings::global().activo();
        const auto resultados = base.buscar(consulta, 5, proveedor.get());
        std::string texto = "Documentos para «" + consulta + "»" +
                            (resultados.empty() ? ": nada." : (resultados.front().vectorial ? " (semántica):" : " (por palabras):"));
        for (const auto& r : resultados) {
            std::string frag = r.texto.size() > 300 ? r.texto.substr(0, 300) + "…" : r.texto;
            for (char& c : frag) if (c == '\n') c = ' ';
            char puntaje[16];
            std::snprintf(puntaje, sizeof(puntaje), "%.2f", r.puntaje);
            texto += "\n· " + r.ruta + (r.pagina > 0 ? " p." + std::to_string(r.pagina) : "") + "  [" + puntaje + "]\n  " + frag;
        }
        agregarMensajeChat(*sesion, texto, MensajeChat::Rol::Estado);
    } else {
        const auto e = base.estado();
        std::string texto = "Documentos del usuario\n  carpeta: " + base.carpeta().string() + "\n  " + resumenEstado(e);
        if (!e.modelo.empty()) texto += "\n  modelo de embeddings: " + e.modelo;
        if (!e.ultimoError.empty()) texto += "\n  último error: " + e.ultimoError;
        texto += "\n/docs actualizar · /docs buscar <texto> · formatos: PDF, DOCX, ODT, PPTX, XLSX, EPUB, HTML, MD, TXT y código";
        agregarMensajeChat(*sesion, texto, MensajeChat::Rol::Estado);
    }
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    return true;
}

void AplicacionTui::agregarItemsDocumentos(nlohmann::json& items) {
    auto& base = docs::BaseDocumentos::global();
    items.push_back({{"kind", "titulo"}, {"label", "Documentos para Silvy (RAG)"}});
    items.push_back({{"kind", "info"}, {"label", "Carpeta"}, {"value", base.carpeta().string()}});
    items.push_back({{"kind", "info"}, {"label", "Estado"}, {"value", resumenEstado(base.estado())}});
    items.push_back({{"kind", "docs-actualizar"}, {"label", "↻ Actualizar los documentos ahora"}});
}

}  // namespace funshi
