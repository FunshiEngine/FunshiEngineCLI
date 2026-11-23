/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo core
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/HuggingFace/HfTypes.h"
#include "Agente/LLM/Local/LocalInference.h"
#include "Agente/LLM/Config/LlmConfig.h"

#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"
#include "Util/Clipboard.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef FUNSHI_VERSION
#define FUNSHI_VERSION "0.0.0"
#endif

namespace funshi {

SeccionTui AplicacionTui::seccionActual() const {
    return static_cast<SeccionTui>(indiceSeccion_);
}

const EstadoSeccion& AplicacionTui::estadoActual() const {
    return estados_[indiceSeccion(seccionActual())];
}

EstadoSeccion& AplicacionTui::estadoActual() {
    return estados_[indiceSeccion(seccionActual())];
}

int AplicacionTui::filasVisibles() const {
    return std::max(1, ftxui::Terminal::Size().dimy - kFilasFueraDePaneles -
                             kFilasEncabezado);
}

int AplicacionTui::filaCajaComandos() const {
    return std::max(0, ftxui::Terminal::Size().dimy - kFilasBarraInferior);
}

ResultadoComando AplicacionTui::llamar(const std::string& comando,
                               const nlohmann::json& argumentos) {
    Solicitud solicitud;
    solicitud.comando = comando;
    solicitud.argumentos = argumentos;
    return ejecutarSolicitud(solicitud);
}

void AplicacionTui::recargarSeccionActual(bool informar) {
    const SeccionTui seccion = seccionActual();
    EstadoSeccion& estado = estados_[indiceSeccion(seccion)];
    estado.desplazamiento = 0;

    // La sección Chat no carga datos del núcleo: muestra su propio
    // historial de auditoría de Silvy.
    if (seccion == SeccionTui::Chat) {
        estado.items = nlohmann::json::array();
        estado.error.clear();
        estado.seleccion = 0;
        return;
    }

    // La sección Modelos HF usa su propio estado (modelosHFItems_, etc.)
    if (seccion == SeccionTui::ModelosHF) {
        estado.items = nlohmann::json::array();
        estado.error.clear();
        estado.seleccion = 0;
        // Al entrar se muestran primero los modelos ya descargados; si no se
        // está a mitad de algo (búsqueda con resultados, variantes, instalación).
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES ||
            (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS && modelosHFItems_.empty())) {
            abrirModelosLocalesHF();
        }
        enfocarModelosHF();
        return;
    }

    // La sección Configuración muestra las rutas persistidas de la CLI.
    if (seccion == SeccionTui::Configuracion) {
        cargarSeccionConfiguracion(estado, informar);
        return;
    }

    nlohmann::json argumentos = nlohmann::json::object();
    if (seccion != SeccionTui::Proyectos) {
        if (proyectoActivo_.empty()) {
            estado.error = "Selecciona un proyecto en la seccion Proyectos (Enter)";
            estado.items = nlohmann::json::array();
            if (informar) {
                mensaje_ = "Sin proyecto activo";
                mensajeEsError_ = true;
            }
            return;
        }
        argumentos["project"] = proyectoActivo_;
    }

    ResultadoComando resultado = llamar(comandoDeSeccion(seccion), argumentos);
    if (resultado.respuesta.correcta) {
        const char* campo = campoItemsDeSeccion(seccion);
        estado.items = resultado.respuesta.resultado.contains(campo)
                           ? resultado.respuesta.resultado[campo]
                           : nlohmann::json::array();
        if (!estado.items.is_array()) estado.items = nlohmann::json::array();
        estado.error.clear();
        estado.seleccion = 0;
        if (informar) {
            mensaje_ = std::string(comandoDeSeccion(seccion)) + ": " +
                       std::to_string(estado.items.size()) + " elemento(s)";
            mensajeEsError_ = false;
        }
    } else {
        estado.items = nlohmann::json::array();
        estado.error = resultado.respuesta.codigoError + ": " +
                       resultado.respuesta.mensajeError;
        if (informar) {
            mensaje_ = estado.error;
            mensajeEsError_ = true;
        }
    }
}

void AplicacionTui::cambiarSeccion(int delta) {
    // Desenfocar ModelosHF si salimos de esa sección
    if (seccionActual() == SeccionTui::ModelosHF && focoEnModelosHF_) {
        desenfocarModelosHF();
    }
    // Desenfocar Chat si salimos de esa sección
    if (seccionActual() == SeccionTui::Chat && focoEnChat_) {
        desenfocarChat();
    }
    indiceSeccion_ = (indiceSeccion_ + delta + kCantidadSecciones) % kCantidadSecciones;
    recargarSeccionActual();
}

void AplicacionTui::irASeccion(int indice) {
    if (indice >= 0 && indice < kCantidadSecciones) {
        // Desenfocar ModelosHF si salimos de esa sección
        if (seccionActual() == SeccionTui::ModelosHF && focoEnModelosHF_) {
            desenfocarModelosHF();
        }
        // Desenfocar Chat si salimos de esa sección
        if (seccionActual() == SeccionTui::Chat && focoEnChat_) {
            desenfocarChat();
        }
        indiceSeccion_ = indice;
        recargarSeccionActual();
    }
}

void AplicacionTui::moverSeleccion(int delta) {
    EstadoSeccion& estado = estadoActual();
    if (estado.items.empty()) return;
    const int tope = static_cast<int>(estado.items.size()) - 1;
    estado.seleccion = std::max(0, std::min(tope, estado.seleccion + delta));
}

void AplicacionTui::irAlInicio() {
    estadoActual().seleccion = 0;
}

void AplicacionTui::irAlFinal() {
    EstadoSeccion& estado = estadoActual();
    estado.seleccion = static_cast<int>(estado.items.size()) - 1;
    if (estado.seleccion < 0) estado.seleccion = 0;
}

void AplicacionTui::accionar() {
    const EstadoSeccion& estado = estadoActual();
    if (estado.items.empty() || estado.seleccion >= static_cast<int>(estado.items.size())) {
        return;
    }
    const nlohmann::json& item = estado.items[estado.seleccion];
    switch (seccionActual()) {
        case SeccionTui::Proyectos: {
            proyectoActivo_ = item.value("name", std::string{});
            mensaje_ = "Proyecto activo: " + proyectoActivo_;
            mensajeEsError_ = false;
            break;
        }
        case SeccionTui::Escena: {
            mensaje_ = "Escena: " + item.value("name", std::string{}) + " (" +
                       item.value("file", std::string{}) + ")";
            mensajeEsError_ = false;
            break;
        }
        case SeccionTui::Scripts: {
            mensaje_ = "Script: " + item.value("name", std::string{});
            mensajeEsError_ = false;
            break;
        }
        case SeccionTui::Diagnosticos: {
            mensaje_ = item.value("message", std::string{});
            mensajeEsError_ =
                item.value("level", std::string{}) == "ERROR" ||
                item.value("level", std::string{}) == "FATAL";
            break;
        }
        case SeccionTui::Chat:
            // El chat gestiona su propia entrada; Enter en una fila no
            // hace nada especial aquí.
            break;
        case SeccionTui::ModelosHF:
            break;
        case SeccionTui::Configuracion:
            editarRutaConfiguracion(item);
            break;
    }
}

void AplicacionTui::cargarSeccionConfiguracion(EstadoSeccion& estado, bool informar) {
    // Rutas editables, en el orden en que se muestran.
    static const std::pair<const char*, const char*> kRutas[] = {
        {"projectsDataDir", "Datos/proyectos del motor"},
        {"localInferenceDir", "Inferencia local (base)"},
        {"modelsDir", "Modelos GGUF"},
        {"hfCacheDir", "Cache de Hugging Face"},
        {"llamaServerPath", "Binario llama-server"},
        {"contextSize", "Contexto (tokens)"},
    };

    const int seleccionPrevia = estado.seleccion;
    const ResultadoComando resultado = llamar("cli.config.get", nlohmann::json::object());
    if (!resultado.respuesta.correcta) {
        estado.items = nlohmann::json::array();
        estado.error = resultado.respuesta.codigoError + ": " +
                       resultado.respuesta.mensajeError;
        if (informar) {
            mensaje_ = estado.error;
            mensajeEsError_ = true;
        }
        return;
    }

    const nlohmann::json& r = resultado.respuesta.resultado;
    const nlohmann::json paths = r.value("paths", nlohmann::json::object());
    const nlohmann::json efectivas = r.value("effectivePaths", nlohmann::json::object());
    const nlohmann::json ajustes = r.value("settings", nlohmann::json::object());

    auto comoTexto = [](const nlohmann::json& obj, const char* clave) -> std::string {
        if (!obj.contains(clave)) return {};
        const auto& v = obj[clave];
        if (v.is_string()) return v.get<std::string>();
        if (v.is_number_integer()) return std::to_string(v.get<long long>());
        return {};
    };

    estado.items = nlohmann::json::array();
    for (const auto& [clave, etiqueta] : kRutas) {
        const bool numerico = ajustes.contains(clave);
        const std::string valor = numerico ? comoTexto(ajustes, clave) : comoTexto(paths, clave);
        estado.items.push_back({
            {"key", clave},
            {"label", etiqueta},
            {"value", numerico && valor == std::to_string(funshi::agente::llm::kContextoPorDefecto)
                          ? std::string{} : valor},
            {"effective", numerico ? valor : comoTexto(efectivas, clave)},
            {"numeric", numerico},
        });
    }
    estado.error.clear();
    const int tope = static_cast<int>(estado.items.size()) - 1;
    estado.seleccion = std::max(0, std::min(tope, seleccionPrevia));
    if (informar) {
        mensaje_ = "Config: " + r.value("configPath", std::string{}) +
                   " · Enter edita la ruta seleccionada";
        mensajeEsError_ = false;
    }
}

void AplicacionTui::editarRutaConfiguracion(const nlohmann::json& item) {
    const std::string clave = item.value("key", std::string{});
    if (clave.empty()) return;
    std::string valor = item.value("value", std::string{});
    if (valor.empty()) valor = item.value("effective", std::string{});
    // Prellenar la caja de comandos; el usuario edita la ruta y pulsa Enter.
    // Dejar la ruta vacía ("") vuelve al valor por defecto.
    if (item.value("numeric", false)) {
        textoComando_ = "cli.config.set " + clave + "=" + valor;
        cursorComando_ = static_cast<int>(textoComando_.size());
    } else {
        textoComando_ = "cli.config.set " + clave + "=\"" + valor + "\"";
        cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
    }
    focoEnEntrada_ = true;
    mensaje_ = "Edita la ruta y pulsa Enter · \"\" = valor por defecto · Esc cancela";
    mensajeEsError_ = false;
}



}  // namespace funshi
