/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Aplicación principal de la TUI (declaración).
#ifndef FUNSHI_TUI_APLICACION_TUI_H
#define FUNSHI_TUI_APLICACION_TUI_H

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/Sesiones/SesionChat.h"
#include "Tui/Chat/ModeloChat.h"
#include "Tui/Configuracion/SelectorColor.h"
#include "Agente/LLM/HuggingFace/HfTypes.h"

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
#include <atomic>
#include <filesystem>
#include <thread>

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

    // Líneas de un mensaje del chat a un ancho dado (prefijo por rol,
    // razonamiento plegado o desplegado). Pura: no toca el estado.
    static void partirMensajeChat(const chat::MensajeChat& m, int ancho, std::vector<chat::LineaChat>& salida);

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
    void cargarSeccionConfiguracion(EstadoSeccion& estado, bool informar);
    void editarRutaConfiguracion(const nlohmann::json& item);
    // Configuración (Configuracion/AplicacionTui_Configuracion.cpp).
    void agregarItemsApariencia(nlohmann::json& items);
    void accionConfiguracion(const nlohmann::json& item);
    bool ordenDePreferencias(const std::string& linea);  // tui.color / tui.idioma
    ftxui::Element filaConfiguracion(const nlohmann::json& item, std::size_t ancho);
    // Documentos del usuario (Chat/AplicacionTui_Documentos.cpp): indexación
    // en segundo plano al abrir y con /docs actualizar; /docs y /docs buscar.
    void iniciarDocumentos(bool aPedido);
    bool ordenDeDocumentos(const std::string& orden, const std::string& resto);
    void aplicarAvisoDocumentos();
    void agregarItemsDocumentos(nlohmann::json& items);
    // Selector de color (Configuracion/AplicacionTui_SelectorColor.cpp).
    tui::SelectorColor selectorColor_;
    void abrirSelectorColor(const std::string& clave);
    bool eventoSelectorColor(ftxui::Event evento);
    ftxui::Element capaSelectorColor();

    void enfocarChat();
    void desenfocarChat();
    bool eventoEnChat(ftxui::Event evento);
    void ejecutarOrdenChat();
    void recorrerHistorialChat(int delta);
    int chatLineasVisibles() const;

    bool eventoEnModelosHF(ftxui::Event evento);
    void enfocarModelosHF();
    void desenfocarModelosHF();
    void iniciarAutoConfiguracionModelo(const funshi::agente::llm::ModelVariant& variant);
    void iniciarConfiguracionModeloLocal(const std::filesystem::path& rutaModelo);
    void reconocerFinConfiguracionModelo();
    void ejecutarBusquedaModelosHF();
    void cargarVariantesModeloSeleccionado(bool autoSeleccionarPrimera);
    void abrirModelosLocalesHF();
    void abrirBusquedaModelosHF();
    void marcarVariantesDescargadas();
    void cargarModelosLocalesHF();

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
    void mostrarToast(const std::string& mensaje, bool error = false);
    ftxui::Element renderToast();
    // Pide un redibujado dentro de `ms` milisegundos (hilo seguro).
    void redibujarTras(int ms);

    ftxui::Element barraSuperior();
    ftxui::Element panelLateral();
    ftxui::Element panelCentral();
    ftxui::Element panelAyuda();
    bool eventoEnAyuda(ftxui::Event evento);
    void abrirAyuda();
    void moverSeleccionAyuda(int delta);
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
    int cursorComando_ = 0;
    std::vector<std::string> historial_;
    int indiceHistorial_ = 0;

    // Chat de Silvy integrado en la TUI (modelo en Tui/Chat/ModeloChat.h).
    using LineaChat = chat::LineaChat;
    using MensajeChat = chat::MensajeChat;
    using SesionTui = chat::SesionTui;
    std::vector<std::shared_ptr<SesionTui>> sesiones_;  // bajo chatMutex_
    std::size_t sesionActiva_ = 0;                       // bajo chatMutex_
    std::unique_ptr<agente::AlmacenSesiones> almacenSesiones_;
    // Sesión a la vista. Llamar con chatMutex_ tomado.
    std::shared_ptr<SesionTui> sesionActualSinLock() const;
    std::shared_ptr<SesionTui> sesionActual() const;
    void cargarSesiones();
    std::shared_ptr<SesionTui> crearSesion(const std::string& titulo);
    void cambiarSesion(int delta);
    void irASesion(std::size_t indice);
    void guardarSesion(const std::shared_ptr<SesionTui>& sesion);
    // /nueva, /sesiones, /sesion N, /renombrar, /borrar, /limpiar, /cancelar.
    // Devuelve true si la línea era una orden de sesión (ya atendida).
    bool ordenDeSesion(const std::string& linea);
    void agregarMensajeChat(SesionTui& sesion, const std::string& texto, MensajeChat::Rol rol,
                            bool enCurso = false);
    void procesarOrdenChat(std::shared_ptr<SesionTui> sesion, const std::string& linea);
    // Mensajes durante el razonamiento y edición (Chat/AplicacionTui_EnCurso.cpp).
    void lanzarOrdenChat(std::shared_ptr<SesionTui> sesion, const std::string& linea, bool agregarLinea = true);
    bool eventoEnCurso(const ftxui::Event& evento);
    void descartarDesde(std::size_t indice, bool editar);
    void siguienteDeLaCola(std::shared_ptr<SesionTui> sesion);
    ftxui::Element barraSesiones(int ancho);
    // Vista de un subagente (AplicacionTui_Subagentes.cpp): clic en su
    // resumen o /sub N la abre; Esc o /volver regresa a la conversación.
    bool ordenDeSubagentes(const std::string& orden, const std::string& resto);
    void abrirSubagente(const std::string& id);
    bool salirDeVistaSubagente();
    bool clicEnChat(int x, int y);
    // Desplaza la conversación a la vista: `lineas` > 0 sube. kChatInicio /
    // kChatFinal van al principio o al final.
    void desplazarChat(int lineas);
    bool desplazarChatConTecla(const ftxui::Event& evento);
    // Líneas del razonamiento a la vista mientras está plegado.
    static constexpr std::size_t kLineasPensamientoPlegado = 2;
    static constexpr int kChatInicio = 1 << 28;
    static constexpr int kChatFinal = -(1 << 28);
    // Filas dibujadas del chat y a qué se refieren ("sub:<id>", "msg:<i>"),
    // para atender el clic. Se rehacen en cada frame.
    std::vector<ftxui::Box> cajasChat_;
    std::vector<std::string> refsChat_;
    // Pide un redibujado limitado a ~30 fps (los tokens llegan más rápido).
    void solicitarRedibujoChat(bool forzar = false);
    std::atomic<long long> ultimoRedibujoChatMs_{0};
    ftxui::Component entradaChat_;
    std::string textoChat_;
    std::vector<std::string> chatHistorialEntrada_;
    int chatIndiceHistorial_ = 0;
    bool focoEnChat_ = false;
    mutable std::mutex chatMutex_;
    bool chatAplicarResultadoPendiente_ = false;
    std::string chatMensajePendiente_;
    bool chatMensajePendienteError_ = false;
    bool chatRecargarPendiente_ = false;

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

    // Estado para selección de variantes/modelos locales
    enum class ModeloHFEstado { LISTA_MODELOS, LISTA_VARIANTES, LISTA_LOCALES, CONFIGURANDO, ERROR_CONFIG };
    ModeloHFEstado modelosHFEstado_ = ModeloHFEstado::LISTA_MODELOS;
    ModeloHFEstado modelosHFEstadoRetorno_ = ModeloHFEstado::LISTA_VARIANTES;
    std::vector<funshi::agente::llm::ModelVariant> modelosHFVariants_;
    int modelosHFVariantSeleccion_ = 0;
    int modelosHFVariantDesplazamiento_ = 0;
    funshi::agente::llm::ModelInfo modeloHFSeleccionadoInfo_;
    std::vector<std::filesystem::path> modelosHFLocales_;
    int modelosHFLocalSeleccion_ = 0;
    int modelosHFLocalDesplazamiento_ = 0;
    bool modelosHFLocalesCargando_ = false;
    // Paralelo a modelosHFLocales_: false si el archivo está truncado.
    std::vector<bool> modelosHFLocalCompleto_;
    // Índice en modelosHFLocales_ del modelo configurado como activo (-1: ninguno).
    int modelosHFLocalActivo_ = -1;
    // Paralelo a modelosHFVariants_: true si esa variante ya está descargada.
    std::vector<bool> modelosHFVariantDescargada_;
    double modelosHFProgreso_ = 0.0;
    int modelosHFFrame_ = 0;  // Para animación de progreso indeterminado
    std::string modelosHFStatus_;

    std::vector<EstadoSeccion> estados_ = std::vector<EstadoSeccion>(kCantidadSecciones);
    int indiceSeccion_ = 0;
    std::string proyectoActivo_;
    std::string cwdCache_;
    std::string mensaje_ = "Listo";
    bool mensajeEsError_ = false;
    bool focoEnEntrada_ = false;
    bool ayudaVisible_ = false;
    int ayudaSeleccion_ = 0;
    int ayudaDesplazamiento_ = 0;

    // ---- Selección con ratón (coordenadas de pantalla) ----
    // Libre: arrastre. Palabra: doble clic. Linea: triple clic.
    enum class ModoSeleccion { Libre, Palabra, Linea };
    ModoSeleccion modoSeleccion_ = ModoSeleccion::Libre;
    bool seleccionActiva_ = false;    // botón presionado
    bool seleccionVisible_ = false;   // hay un resaltado dibujado
    int seleccionInicioX_ = 0;
    int seleccionInicioY_ = 0;
    int seleccionFinX_ = 0;
    int seleccionFinY_ = 0;
    bool copiarAlSoltar_ = false;     // el botón se soltó: el render copia
    bool copiaPendiente_ = false;     // el render pidió copiar
    std::string textoSeleccionado_;   // texto leído de la pantalla (último render)
    std::string textoCopiado_;        // último texto copiado (evita repetir)
    // Clics seguidos en la misma celda para doble/triple clic.
    int clicsSeguidos_ = 0;
    int ultimoClicX_ = -1;
    int ultimoClicY_ = -1;
    std::chrono::steady_clock::time_point ultimoClic_{};
    std::chrono::steady_clock::time_point ultimoMovimiento_{};
    static constexpr int kDobleClicMs = 400;
    // Si la terminal no informa el soltar del botón, se copia tras esta pausa.
    static constexpr int kCopiaInactividadMs = 600;
    void lanzarCopia(const std::string& texto);

    // ---- Toast (solo se toca desde el hilo de la TUI) ----
    bool toastVisible_ = false;
    std::string toastMensaje_;
    bool toastError_ = false;
    std::chrono::steady_clock::time_point toastInicio_;
    static constexpr int kToastDuracionMs = 2200;

    // Estado compartido con los hilos que piden redibujados: permite que un
    // hilo dormido no toque la pantalla si la aplicación ya fue destruida.
    struct EstadoCompartido {
        std::mutex mutex;
        ftxui::ScreenInteractive* pantalla = nullptr;
        // Resultado de la copia hecha en segundo plano (protegido por mutex).
        bool copiaLista = false;
        bool copiaOk = false;
        std::string copiaMetodo;
        std::string copiaError;
        std::string copiaTexto;
        // Hay un redibujado programado para la copia por inactividad.
        std::atomic<bool> temporizadorCopia{false};
        // Avance de la indexación de documentos (hilo de fondo → barra de estado).
        std::string avisoDocumentos;
        bool avisoDocumentosError = false;
        bool avisoDocumentosPendiente = false;
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
