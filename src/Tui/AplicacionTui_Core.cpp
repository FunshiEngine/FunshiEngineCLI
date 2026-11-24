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
#include "Agente/LLM/Local/LlamaBinarios.h"
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


AplicacionTui::AplicacionTui(ftxui::ScreenInteractive* pantalla,
              Auditoria& auditoria, Harness& harness, Silvy& silvy)
    : pantalla_(pantalla), auditoria_(auditoria),
      harness_(harness), silvy_(silvy) {
    compartido_->pantalla = pantalla_;

    ftxui::InputOption opciones;
    opciones.content = &textoComando_;
    opciones.cursor_position = &cursorComando_;
    opciones.placeholder = "comando del catalogo, por ejemplo project.list";
    opciones.multiline = false;
    // El comando se ejecuta al recibir Enter, no con on_enter: FTXUI difiere
    // ese callback a la cola de tareas y se ejecutaria con el texto ya
    // modificado si mas teclas llegan en el mismo lote.
    opciones.transform = [](const ftxui::InputState& estado) {
        ftxui::Element elemento = estado.element;
        if (estado.focused) {
            elemento = elemento | ftxui::color(kAcento) | ftxui::bold;
        } else {
            elemento = elemento | ftxui::color(kTextoTenue);
        }
        return elemento;
    };
    entrada_ = ftxui::Input(opciones);
    Add(entrada_);

    // Input dedicado del chat de Silvy (órdenes en lenguaje natural o JSON).
    ftxui::InputOption opcionesChat;
    opcionesChat.content = &textoChat_;
    opcionesChat.placeholder = "orden de Silvy, ej. 'crear proyecto MiJuego 2d'";
    opcionesChat.multiline = false;
    opcionesChat.transform = [](const ftxui::InputState& estado) {
        ftxui::Element elemento = estado.element;
        if (estado.focused) {
            elemento = elemento | ftxui::color(kAcento) | ftxui::bold;
        } else {
            elemento = elemento | ftxui::color(kTextoTenue);
        }
        return elemento;
    };
    entradaChat_ = ftxui::Input(opcionesChat);
    Add(entradaChat_);

    // Input para búsqueda de modelos HF.
    ftxui::InputOption opcionesModelosHF;
    opcionesModelosHF.content = &textoModelosHF_;
    opcionesModelosHF.placeholder = "buscar modelo (ej. llama 3.2, phi-4, qwen)...";
    opcionesModelosHF.multiline = false;
    opcionesModelosHF.transform = [](const ftxui::InputState& estado) {
        ftxui::Element elemento = estado.element;
        if (estado.focused) {
            elemento = elemento | ftxui::color(kAcento) | ftxui::bold;
        } else {
            elemento = elemento | ftxui::color(kTextoTenue);
        }
        return elemento;
    };
    entradaModelosHF_ = ftxui::Input(opcionesModelosHF);
    Add(entradaModelosHF_);

    // Necesario para recibir Pressed/Moved/Released del ratón en la TUI.
    if (pantalla_) {
        pantalla_->TrackMouse(true);
    }

    // Sesiones de chat guardadas (o una nueva si no hay).
    cargarSesiones();

    recargarSeccionActual();

    // Al iniciar: descargar llama-server oficial si falta o actualizarlo si
    // hay una versión nueva. En segundo plano y sin fallar si no hay red.
    std::thread([estado = compartido_]() {
        try {
            const auto cfg = funshi::agente::llm::LlmConfig::load();
            if (cfg.llamaServerPath.empty()) {
                funshi::agente::llm::InstaladorLlama instalador(cfg.localInferenceDirEfectivo());
                instalador.asegurarActualizado([estado](double, const std::string&) {
                    std::lock_guard<std::mutex> lock(estado->mutex);
                    if (estado->pantalla) estado->pantalla->PostEvent(ftxui::Event());
                });
            }
        } catch (...) {
            // Nunca debe tumbar la TUI.
        }
        std::lock_guard<std::mutex> lock(estado->mutex);
        if (estado->pantalla) estado->pantalla->PostEvent(ftxui::Event());
    }).detach();
}

AplicacionTui::~AplicacionTui() {
    // Los hilos de redibujado que sigan dormidos ya no tocarán la pantalla.
    std::lock_guard<std::mutex> bloqueo(compartido_->mutex);
    compartido_->pantalla = nullptr;
}

ftxui::Element AplicacionTui::OnRender() {
    const bool enChat = seccionActual() == SeccionTui::Chat;
    const bool enModelosHF = seccionActual() == SeccionTui::ModelosHF;
    const bool mostrarCajaComandosGlobal = !enChat && !enModelosHF;

    // Animación para progreso indeterminado en CONFIGURANDO
    if (enModelosHF && (modelosHFEstado_ == ModeloHFEstado::CONFIGURANDO || modelosHFEstado_ == ModeloHFEstado::ERROR_CONFIG)) {
        modelosHFFrame_ = (modelosHFFrame_ + 1) % 12;
    }

    std::vector<ftxui::Element> filas;
    filas.push_back(barraSuperior());
    filas.push_back(
        ftxui::hbox({
            panelLateral(),
            ftxui::separator(),
            panelCentral(),
        }) | ftxui::flex);

    if (mostrarCajaComandosGlobal) {
        filas.push_back(ftxui::separator());
        filas.push_back(cajaComandos());
    }

    filas.push_back(ftxui::separator());
    filas.push_back(ftxui::text(" " + mensaje_ + " ") |
                    (mensajeEsError_ ? ftxui::color(kError) : ftxui::color(kTextoTenue)) |
                    ftxui::bgcolor(kFondoBarra));

    // Atajos contextuales según la sección y estado
    std::string atajos;
    if (enChat) {
        atajos = " ↑↓ historial · Enter ejecuta · Esc vuelve · / JSON directo ";
    } else if (enModelosHF) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
            atajos = focoEnCajaBusquedaModelosHF_
                ? " / escribe y Enter busca · Esc vuelve a lista/salir "
                : " ↑↓ mover · Enter selecciona · l locales · r reintenta · c limpia cache · Esc vuelve ";
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            atajos = " ↑↓ mover · Enter configurar · l locales · r reintenta · c limpia cache · Esc vuelve ";
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            atajos = " ↑↓ mover · Enter usar modelo · r recargar · Esc vuelve ";
        } else if (modelosHFEstado_ == ModeloHFEstado::CONFIGURANDO) {
            atajos = " Configurando modelo... · Esc para volver ";
        } else { // ERROR_CONFIG
            atajos = " Error en configuración · Esc para volver ";
        }
    } else if (ayudaVisible_) {
        atajos = " ↑↓ elegir comando · Enter usar ejemplo · Esc/? cerrar ayuda ";
    } else {
        atajos = textoAtajos();
    }

    filas.push_back(ftxui::text(atajos) |
                    ftxui::color(kTextoTenue) | ftxui::bgcolor(kFondoBarra));

    // Contenido principal (con resaltado/lectura de selección) + toast encima.
    // El toast va después del envoltorio, así nunca entra en el texto copiado.
    ftxui::Element contenido =
        ftxui::vbox(std::move(filas)) | ftxui::color(kTexto) | ftxui::bgcolor(kFondo);

    ftxui::Elements elements;
    elements.push_back(conSeleccion(std::move(contenido)));
    elements.push_back(renderToast());
    return ftxui::dbox(std::move(elements));
}

bool AplicacionTui::OnEvent(ftxui::Event evento) {
    // Aviso que el render se manda a sí mismo para copiar fuera del dibujado.
    if (evento == ftxui::Event::Custom) {
        procesarCopiaPendiente();
        bool recargar = false;
        std::string mensajePendiente;
        bool errorPendiente = false;
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            if (chatAplicarResultadoPendiente_) {
                chatAplicarResultadoPendiente_ = false;
                mensajePendiente = chatMensajePendiente_;
                errorPendiente = chatMensajePendienteError_;
                recargar = chatRecargarPendiente_;
                chatRecargarPendiente_ = false;
                chatMensajePendiente_.clear();
            }
        }
        if (!mensajePendiente.empty() || recargar) {
            if (!mensajePendiente.empty()) {
                mensaje_ = mensajePendiente;
                mensajeEsError_ = errorPendiente;
            }
            if (recargar) {
                recargarSeccionActual(false);
            }
        }
        return true;
    }
    // El ratón se atiende siempre primero: la selección/copia no debe
    // depender del foco de las cajas de texto.
    if (evento.is_mouse()) {
        return manejarRaton(evento);
    }
    // Cualquier tecla descarta el resaltado. Los redibujados internos
    // (Event vacío) no cuentan.
    if (seleccionVisible_ && !evento.input().empty()) {
        limpiarSeleccion();
    }
    if (focoEnModelosHF_) {
        return eventoEnModelosHF(evento);
    }
    if (focoEnChat_) {
        return eventoEnChat(evento);
    }
    if (focoEnEntrada_) {
        return eventoEnEntrada(evento);
    }
    if (ayudaVisible_) {
        return eventoEnAyuda(evento);
    }
    if (evento == ftxui::Event::Character('q') || evento == ftxui::Event::CtrlC ||
        evento == ftxui::Event::Character('\x03')) {
        pantalla_->Exit();
        return true;
    }
    if (evento == ftxui::Event::Escape) {
        // En la vista de un subagente, Esc regresa a la conversación.
        if (seccionActual() == SeccionTui::Chat && salirDeVistaSubagente()) return true;
        pantalla_->Exit();
        return true;
    }
    if (evento == ftxui::Event::Character('r')) {
        recargarSeccionActual();
        return true;
    }
    if (evento == ftxui::Event::Character('/')) {
        if (seccionActual() == SeccionTui::Chat) {
            enfocarChat();
            return true;
        }
        if (seccionActual() == SeccionTui::ModelosHF) {
            enfocarModelosHF();
            return true;
        }
        enfocarEntrada();
        return true;
    }
    if (evento == ftxui::Event::Character('?')) {
        abrirAyuda();
        return true;
    }
    if (seccionActual() == SeccionTui::Chat && desplazarChatConTecla(evento)) return true;
    if (evento == ftxui::Event::ArrowUp) {
        moverSeleccion(-1);
        return true;
    }
    if (evento == ftxui::Event::ArrowDown) {
        moverSeleccion(1);
        return true;
    }
    if (evento == ftxui::Event::PageUp) {
        moverSeleccion(-filasVisibles());
        return true;
    }
    if (evento == ftxui::Event::PageDown) {
        moverSeleccion(filasVisibles());
        return true;
    }
    if (evento == ftxui::Event::Home) {
        irAlInicio();
        return true;
    }
    if (evento == ftxui::Event::End) {
        irAlFinal();
        return true;
    }
    if (evento == ftxui::Event::ArrowLeft || evento == ftxui::Event::Tab) {
        cambiarSeccion(-1);
        return true;
    }
    if (evento == ftxui::Event::ArrowRight) {
        cambiarSeccion(1);
        return true;
    }
    if (evento == ftxui::Event::Return) {
        accionar();
        return true;
    }
    return false;
}

ftxui::Component AplicacionTui::ActiveChild() {
    if (focoEnModelosHF_) return entradaModelosHF_;
    if (focoEnChat_) return entradaChat_;
    return focoEnEntrada_ ? entrada_ : nullptr;
}


}  // namespace funshi
