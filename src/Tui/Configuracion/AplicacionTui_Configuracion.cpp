/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — sección Configuración: rutas y modelo (cli.config.*),
// idioma de la interfaz y colores de la paleta (Terminal/PreferenciasTui).
//
// Enter sobre una ruta o un color prellena la caja de comandos; los colores
// y el idioma usan órdenes propias de la TUI, que no pasan por la CLI:
//   tui.color <clave>=#RRGGBB   (vacío = valor por defecto)
//   tui.idioma <código>
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/PreferenciasTui.h"
#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"

#include <algorithm>
#include <sstream>

namespace funshi {

namespace {

std::string nombreIdioma(const std::string& codigo) {
    for (const auto& i : tui::idiomas()) {
        if (codigo == i.codigo) return std::string(i.nombre) + " (" + i.codigo + ")";
    }
    return codigo;
}

std::string columna(std::string s, std::size_t ancho) {
    s = recortar(s, ancho);
    const int falta = static_cast<int>(ancho) - ftxui::string_width(s);
    return falta > 0 ? s + std::string(static_cast<std::size_t>(falta), ' ') : s;
}

}  // namespace

void AplicacionTui::agregarItemsApariencia(nlohmann::json& items) {
    items.push_back({{"kind", "titulo"}, {"label", "Interfaz"}});
    items.push_back({{"kind", "idioma"}, {"label", "Idioma"}, {"value", tui::idiomaActual()}});
    items.push_back({{"kind", "titulo"}, {"label", "Colores (clic o Enter abre el selector)"}});
    for (const auto& c : tui::coloresTema()) {
        items.push_back({{"kind", "color"},
                         {"key", c.clave},
                         {"label", c.nombre},
                         {"value", tui::aHex(c.actual)},
                         {"default", c.actual == c.porDefecto},
                         {"uso", c.uso}});
    }
    items.push_back({{"kind", "restablecer"}, {"label", "↺ Restablecer todos los colores"}});
    agregarItemsDocumentos(items);
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
    estado.items.push_back({{"kind", "titulo"}, {"label", "Rutas y modelo"}});
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
            {"kind", "ruta"},
        });
    }
    agregarItemsApariencia(estado.items);
    estado.error.clear();
    const int tope = static_cast<int>(estado.items.size()) - 1;
    estado.seleccion = std::max(0, std::min(tope, seleccionPrevia));
    if (informar) {
        mensaje_ = "Config: " + r.value("configPath", std::string{}) +
                   " · Enter edita la ruta, el idioma o el color seleccionado";
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

void AplicacionTui::accionConfiguracion(const nlohmann::json& item) {
    const std::string tipo = item.value("kind", std::string{});
    if (tipo == "ruta") {
        editarRutaConfiguracion(item);
    } else if (tipo == "idioma") {
        tui::fijarIdioma(tui::siguienteIdioma());
        tui::guardarPreferencias();
        mensaje_ = "Idioma: " + nombreIdioma(tui::idiomaActual()) +
                   " · por ahora solo se guarda: los textos siguen en español";
        mensajeEsError_ = false;
        recargarSeccionActual(false);
    } else if (tipo == "color") {
        abrirSelectorColor(item.value("key", std::string{}));
    } else if (tipo == "docs-actualizar") {
        iniciarDocumentos(true);
        mensaje_ = "Documentos: indexando…";
        mensajeEsError_ = false;
    } else if (tipo == "restablecer") {
        tui::restablecerColores();
        tui::guardarPreferencias();
        mensaje_ = "Colores restablecidos";
        mensajeEsError_ = false;
        recargarSeccionActual(false);
    }
}

bool AplicacionTui::ordenDePreferencias(const std::string& linea) {
    std::istringstream in(linea);
    std::string orden, resto;
    in >> orden;
    std::getline(in, resto);
    resto.erase(0, resto.find_first_not_of(' '));
    while (!resto.empty() && resto.back() == ' ') resto.pop_back();
    if (orden == "tui.idioma") {
        if (!tui::fijarIdioma(resto)) {
            std::string lista;
            for (const auto& i : tui::idiomas()) lista += std::string(lista.empty() ? "" : ", ") + i.codigo;
            mensaje_ = "Idioma desconocido. Opciones: " + lista;
            mensajeEsError_ = true;
            return true;
        }
        tui::guardarPreferencias();
        mensaje_ = "Idioma: " + nombreIdioma(resto) + " · por ahora solo se guarda";
        mensajeEsError_ = false;
    } else if (orden == "tui.color") {
        const auto igual = resto.find('=');
        const std::string clave = resto.substr(0, igual);
        const std::string valor = igual == std::string::npos ? std::string() : resto.substr(igual + 1);
        std::string error;
        if (!tui::fijarColor(clave, valor, error)) {
            mensaje_ = error;
            mensajeEsError_ = true;
            return true;  // la orden queda escrita para corregirla
        }
        tui::guardarPreferencias();
        const tui::ColorTema* c = tui::colorTema(clave);
        mensaje_ = std::string(c->nombre) + ": " + tui::aHex(c->actual) +
                   (c->actual == c->porDefecto ? " (por defecto)" : "");
        mensajeEsError_ = false;
    } else {
        return false;
    }
    historial_.push_back(linea);
    indiceHistorial_ = static_cast<int>(historial_.size());
    textoComando_.clear();
    cursorComando_ = 0;
    focoEnEntrada_ = false;
    recargarSeccionActual(false);
    return true;
}

ftxui::Element AplicacionTui::filaConfiguracion(const nlohmann::json& item, std::size_t ancho) {
    const std::string tipo = item.value("kind", std::string{});
    const std::string etiqueta = item.value("label", std::string{});
    if (tipo == "titulo") {
        return ftxui::text(" " + recortar(etiqueta, ancho)) | ftxui::bold | ftxui::color(kAcento);
    }
    if (tipo == "restablecer" || tipo == "docs-actualizar") {
        return ftxui::text("  " + recortar(etiqueta, ancho)) | ftxui::color(kTexto);
    }
    if (tipo == "info") {
        return ftxui::hbox({ftxui::text(columna("  " + etiqueta, 30)),
                            ftxui::text(recortar(item.value("value", std::string{}), ancho > 32 ? ancho - 32 : 8)) |
                                ftxui::color(kTextoTenue)});
    }
    if (tipo == "idioma") {
        return ftxui::hbox({
            ftxui::text(columna("  " + etiqueta, 30)),
            ftxui::text(nombreIdioma(item.value("value", std::string{}))) | ftxui::color(kAcento),
            ftxui::filler(),
            ftxui::text(" solo visual por ahora ") | ftxui::color(kTextoTenue),
        });
    }
    if (tipo == "color") {
        tui::Rgb rgb;
        tui::leerColor(item.value("value", std::string{}), rgb);
        const bool porDefecto = item.value("default", true);
        const std::size_t anchoUso = ancho > 64 ? ancho - 64 : 4;
        return ftxui::hbox({
            ftxui::text(columna("  " + etiqueta, 30)),
            ftxui::text("      ") | ftxui::bgcolor(ftxui::Color::RGB(rgb.r, rgb.g, rgb.b)),
            ftxui::text(" " + item.value("value", std::string{}) + "  ") | ftxui::color(kTexto),
            ftxui::text(recortar(item.value("uso", std::string{}), anchoUso)) | ftxui::color(kTextoTenue),
            ftxui::filler(),
            ftxui::text(porDefecto ? " por defecto " : " personalizado ") |
                ftxui::color(porDefecto ? kTextoTenue : kExito),
        });
    }
    // Ruta o ajuste de la CLI.
    const std::string valor = item.value("value", std::string{});
    const std::string efectiva = item.value("effective", std::string{});
    const bool porDefecto = valor.empty();
    const std::string ruta = porDefecto ? efectiva : valor;
    const std::size_t anchoRuta = ancho > 44 ? ancho - 44 : 10;
    return ftxui::hbox({
        ftxui::text(columna("  " + etiqueta, 30)),
        ftxui::text(recortar(ruta.empty() ? "(sin definir)" : ruta, anchoRuta)) |
            ftxui::color(porDefecto ? kTextoTenue : kAcento),
        ftxui::filler(),
        ftxui::text(porDefecto ? " por defecto " : " personalizada ") |
            ftxui::color(porDefecto ? kTextoTenue : kExito),
    });
}

}  // namespace funshi
