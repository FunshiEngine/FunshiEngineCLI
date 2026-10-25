/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Aplicación principal de la TUI (declaración).
#ifndef FUNSHI_TUI_APLICACION_TUI_H
#define FUNSHI_TUI_APLICACION_TUI_H

#include "Tui/TemaTui.h"
#include "Tui/UtilTui.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/HfTypes.h"

#include "Nucleo/Respuesta.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace funshi {

struct EstadoSeccion {
    nlohmann::json items = nlohmann::json::array();
    std::string error;
    int seleccion = 0;
    int desplazamiento = 0;
};

class AplicacionTui : public ftxui::ComponentBase {
 public:
    AplicacionTui(ftxui::ScreenInteractive* pantalla,
                  Auditoria& auditoria, Harness& harness, Silvy& silvy);
    ~AplicacionTui() override;

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event evento) override;
    ftxui::Component ActiveChild() override;

    void irASeccion(int indice);

 private:
    SeccionTui seccionActual() const;
    const EstadoSeccion& estadoActual() const;
    EstadoSeccion& estadoActual();
    int filasVisibles() const;
    int filaCajaComandos() const;

    static ResultadoComando llamar(const std::string& comando,
                                   const nlohmann::json& argumentos);
    void recargarSeccionActual(bool informar = true);
    void cambiarSeccion(int delta);
    void moverSeleccion(int delta);
    void irAlInicio();
    void irAlFinal();
    void accionar();

    bool eventoEnEntrada(ftxui::Event evento);
    void enfocarEntrada();
    void desenfocarEntrada();
    void ejecutarComandoEscrito();
    static std::string resumenResultado(const nlohmann::json& resultado);
    void recorrerHistorial(int delta);

    void enfocarChat();
    void desenfocarChat();
    bool eventoEnChat(ftxui::Event evento);
    void ejecutarOrdenChat();
    void recorrerHistorialChat(int delta);

    bool eventoEnModelosHF(ftxui::Event evento);
    void enfocarModelosHF();
    void desenfocarModelosHF();
    void iniciarAutoConfiguracionModelo(const funshi::agente::llm::ModelVariant& variant);
    void reconocerFinConfiguracionModelo();
    void ejecutarBusquedaModelosHF();
    void cargarVariantesModeloSeleccionado(bool autoSeleccionarPrimera);

    // ---- Ratón: selección resaltada + copia automática ----
    bool manejarRaton(ftxui::Event& evento);
    // Última fila (inclusive) del área de paneles para la sección actual.
    int ultimaFilaPanel(int altoPantalla) const;
    void limpiarSeleccion();
    // Envuelve el contenido: tras dibujarlo pinta el resaltado y lee de la
    // pantalla el texto resaltado.
    ftxui::Element conSeleccion(ftxui::Element contenido);
    void aplicarSeleccion(ftxui::Screen& pantalla);
    void procesarCopiaPendiente();
    void mostrarToast(const std::string& mensaje);
    ftxui::Element renderToast();
    // Pide un redibujado dentro de `ms` milisegundos (hilo seguro).
    void redibujarTras(int ms);

    ftxui::Element barraSuperior();
    ftxui::Element panelLateral();
    ftxui::Element panelCentral();
    ftxui::Element panelAyuda();
    ftxui::Element filaDeItem(SeccionTui seccion, const nlohmann::json& item, std::size_t ancho);
    ftxui::Element panelChat();
    ftxui::Element panelModelosHF();
    ftxui::Element cajaComandos();

    // ---- Miembros ----

    ftxui::ScreenInteractive* pantalla_ = nullptr;
    Auditoria& auditoria_;
    Harness& harness_;
    Silvy& silvy_;

    ftxui::Component entrada_;
    std::string textoComando_;
    std::vector<std::string> historial_;
    int indiceHistorial_ = 0;

    // Chat de Silvy integrado en la TUI.
    ftxui::Component entradaChat_;
    std::string textoChat_;
    std::vector<std::string> chatHistorial_;
    int chatIndiceHistorial_ = 0;
    int chatDesplazamiento_ = 0;
    bool focoEnChat_ = false;

    // Modelos HF integrado en la TUI.
    ftxui::Component entradaModelosHF_;
    std::string textoModelosHF_;
    std::vector<funshi::agente::llm::ModelInfo> modelosHFItems_;
    int modelosHFSeleccion_ = 0;
    int modelosHFDesplazamiento_ = 0;
    bool focoEnModelosHF_ = false;
    bool focoEnCajaBusquedaModelosHF_ = false;
    bool modelosHFBuscando_ = false;
    bool modelosHFVariantsCargando_ = false;
    std::string modelosHFError_;
    std::mutex modelosHFMutex_;

    // Estado para selección de variantes
    enum class ModeloHFEstado { LISTA_MODELOS, LISTA_VARIANTES, CONFIGURANDO, ERROR_CONFIG };
    ModeloHFEstado modelosHFEstado_ = ModeloHFEstado::LISTA_MODELOS;
    std::vector<funshi::agente::llm::ModelVariant> modelosHFVariants_;
    int modelosHFVariantSeleccion_ = 0;
    funshi::agente::llm::ModelInfo modeloHFSeleccionadoInfo_;
    double modelosHFProgreso_ = 0.0;
    int modelosHFFrame_ = 0;  // Para animación de progreso indeterminado
    std::string modelosHFStatus_;

    std::vector<EstadoSeccion> estados_ = std::vector<EstadoSeccion>(kCantidadSecciones);
    int indiceSeccion_ = 0;
    std::string proyectoActivo_;
    std::string mensaje_ = "Listo";
    bool mensajeEsError_ = false;
    bool focoEnEntrada_ = false;
    bool ayudaVisible_ = false;

    // ---- Selección con ratón (coordenadas de pantalla) ----
    bool seleccionActiva_ = false;    // botón presionado, arrastrando
    bool seleccionVisible_ = false;   // hay un resaltado dibujado
    int seleccionInicioX_ = 0;
    int seleccionInicioY_ = 0;
    int seleccionFinX_ = 0;
    int seleccionFinY_ = 0;
    bool copiarAlSoltar_ = false;     // el botón se soltó: copia final
    bool copiaPendiente_ = false;     // el render pidió copiar
    bool toastPendiente_ = false;     // el render pidió mostrar el toast
    std::string textoSeleccionado_;   // texto leído de la pantalla (último render)
    std::string textoCopiado_;        // último texto enviado al portapapeles
    bool redibujoProgramado_ = false;
    std::chrono::steady_clock::time_point ultimaCopia_{};
    // Mínimo entre copias durante el arrastre (la copia final no lo respeta).
    static constexpr int kCopiaMinMs = 120;

    // ---- Toast (solo se toca desde el hilo de la TUI) ----
    bool toastVisible_ = false;
    std::string toastMensaje_;
    std::chrono::steady_clock::time_point toastInicio_;
    static constexpr int kToastDuracionMs = 1500;

    // Estado compartido con los hilos que piden redibujados: permite que un
    // hilo dormido no toque la pantalla si la aplicación ya fue destruida.
    struct EstadoCompartido {
        std::mutex mutex;
        ftxui::ScreenInteractive* pantalla = nullptr;
    };
    std::shared_ptr<EstadoCompartido> compartido_ = std::make_shared<EstadoCompartido>();
};

ftxui::Component crearAplicacionTui(ftxui::ScreenInteractive* pantalla,
                                    Auditoria& auditoria,
                                    Harness& harness,
                                    Silvy& silvy,
                                    int seccionInicial = 0);

}  // namespace funshi

#endif
