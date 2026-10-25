/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo core
#include "Tui/AplicacionTui.h"

#include "Tui/TemaTui.h"
#include "Tui/UtilTui.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/HfApi.h"
#include "Agente/LLM/HfTypes.h"
#include "Agente/LLM/LocalInference.h"

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

bool AplicacionTui::eventoEnEntrada(ftxui::Event evento) {
    if (evento == ftxui::Event::CtrlC || evento == ftxui::Event::Character('\x03')) {
        // Interrumpir la TUI tiene que funcionar también con el foco en la
        // caja de comandos: si no, el carácter se escribiría en ella.
        pantalla_->Exit();
        return true;
    }
    if (evento == ftxui::Event::Escape) {
        desenfocarEntrada();
        return true;
    }
    if (evento == ftxui::Event::ArrowUp) {
        recorrerHistorial(-1);
        return true;
    }
    if (evento == ftxui::Event::ArrowDown) {
        recorrerHistorial(1);
        return true;
    }
    if (evento == ftxui::Event::Return) {
        // El comando se ejecuta aqui y no con on_enter del input: ese
        // callback lo difiere FTXUI al final del lote de eventos, y se
        // ejecutaria con el texto que hubiera al terminar el lote.
        ejecutarComandoEscrito();
        return true;
    }
    return entrada_->OnEvent(evento);
}

void AplicacionTui::enfocarEntrada() {
    focoEnEntrada_ = true;
    mensaje_ = "Escribe un comando y pulsa Enter (Esc para volver)";
    mensajeEsError_ = false;
}

void AplicacionTui::desenfocarEntrada() {
    focoEnEntrada_ = false;
}

void AplicacionTui::ejecutarComandoEscrito() {
    const std::string linea = textoComando_;
    const std::vector<Token> tokens = tokenizar(linea);
    if (tokens.empty()) return;

    nlohmann::json argumentos = nlohmann::json::object();
    std::string error;
    if (!construirArgumentos(tokens, argumentos, error)) {
        mensaje_ = error;
        mensajeEsError_ = true;
        return;
    }

    if (!linea.empty()) {
        historial_.push_back(linea);
        indiceHistorial_ = static_cast<int>(historial_.size());
    }

    const std::string comando = tokens[0].texto;
    const ResultadoComando resultado = llamar(comando, argumentos);
    if (resultado.respuesta.correcta) {
        mensaje_ = comando + ": ok" + resumenResultado(resultado.respuesta.resultado);
        mensajeEsError_ = false;
    } else {
        mensaje_ = comando + ": " + resultado.respuesta.codigoError + " - " +
                   resultado.respuesta.mensajeError;
        mensajeEsError_ = true;
    }
    textoComando_.clear();
    recargarSeccionActual(false);
}

std::string AplicacionTui::resumenResultado(const nlohmann::json& resultado) {
    if (!resultado.is_object()) return {};
    if (resultado.contains("count") && resultado["count"].is_number_integer()) {
        return " (" + std::to_string(resultado["count"].get<long long>()) + ")";
    }
    if (resultado.contains("name") && resultado["name"].is_string()) {
        return " (" + resultado["name"].get<std::string>() + ")";
    }
    return {};
}

void AplicacionTui::recorrerHistorial(int delta) {
    if (historial_.empty()) return;
    if (indiceHistorial_ < 0) indiceHistorial_ = static_cast<int>(historial_.size());
    indiceHistorial_ = std::max(
        0, std::min(static_cast<int>(historial_.size()), indiceHistorial_ + delta));
    textoComando_ = indiceHistorial_ < static_cast<int>(historial_.size())
                        ? historial_[indiceHistorial_]
                        : std::string();
}


}  // namespace funshi
