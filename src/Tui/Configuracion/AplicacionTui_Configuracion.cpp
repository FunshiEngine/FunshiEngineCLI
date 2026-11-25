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

#include "Agente/LLM/Config/LlmConfig.h"

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
    const bool esProveedores = seccionActual() == SeccionTui::Proveedores;
    const int seleccionPrevia = estado.seleccion;
    const ResultadoComando resultado = llamar("cli.config.get", nlohmann::json::object());
    if (!resultado.respuesta.correcta) {
        estado.items = nlohmann::json::array();
        estado.error = resultado.respuesta.codigoError + ": " + resultado.respuesta.mensajeError;
        if (informar) { mensaje_ = estado.error; mensajeEsError_ = true; }
        return;
    }

    const nlohmann::json& r = resultado.respuesta.resultado;
    const nlohmann::json paths = r.value("paths", nlohmann::json::object());
    const nlohmann::json efectivas = r.value("effectivePaths", nlohmann::json::object());
    const nlohmann::json ajustes = r.value("settings", nlohmann::json::object());
    const nlohmann::json llm = r.value("llm", nlohmann::json::object());
    auto comoTexto = [](const nlohmann::json& obj, const char* clave) -> std::string {
        if (!obj.contains(clave)) return {};
        const auto& v = obj[clave];
        if (v.is_string()) return v.get<std::string>();
        if (v.is_number_integer()) return std::to_string(v.get<long long>());
        return {};
    };

    estado.items = nlohmann::json::array();
    if (!esProveedores) {
        static const std::pair<const char*, const char*> kRutas[] = {
            {"projectsDataDir", "Datos/proyectos del motor"},
            {"localInferenceDir", "Inferencia local (base)"},
            {"modelsDir", "Modelos GGUF"},
            {"hfCacheDir", "Cache de Hugging Face"},
            {"llamaServerPath", "Binario llama-server"},
            {"contextSize", "Contexto (tokens)"},
        };
        estado.items.push_back({{"kind", "titulo"}, {"label", "Rutas y modelo"}});
        for (const auto& [clave, etiqueta] : kRutas) {
            const bool numerico = ajustes.contains(clave);
            const std::string valor = numerico ? comoTexto(ajustes, clave) : comoTexto(paths, clave);
            estado.items.push_back({
                {"key", clave}, {"label", etiqueta},
                {"value", numerico && valor == std::to_string(funshi::agente::llm::kContextoPorDefecto) ? std::string{} : valor},
                {"effective", numerico ? valor : comoTexto(efectivas, clave)},
                {"numeric", numerico}, {"kind", "ruta"},
            });
        }
        agregarItemsApariencia(estado.items);
    } else {
        const std::string providerActual = llm.value("provider", std::string{});
        const std::string modelRef = llm.value("modelRef", std::string{});
        estado.items.push_back({{"kind", "titulo"}, {"label", "Proveedor activo"}});
        estado.items.push_back({{"kind","info"},{"label","Proveedor"},{"value",providerActual.empty()?"(ninguno)":providerActual}});
        estado.items.push_back({{"kind","info"},{"label","Modelo activo"},{"value",modelRef.empty()?llm.value("modelName",std::string{}):modelRef}});
        estado.items.push_back({{"kind","info"},{"label","Pensamiento"},{"value",llm.value("thinking",std::string("off"))}});
        estado.items.push_back({{"kind","info"},{"label","Calidad/power"},{"value",llm.value("power",std::string("medium"))}});
        estado.items.push_back({{"kind","titulo"},{"label","Proveedores (Enter selecciona/expande)"}});
        struct Prov { const char* id; const char* nombre; const char* keyField; bool hasKey; };
        std::vector<Prov> provs = {
            {"google","Google Gemini (OpenAI compatible)","googleApiKey", llm.value("hasGoogleApiKey",false)},
            {"nvidia","NVIDIA NIM (OpenAI compatible)","nvidiaApiKey", llm.value("hasNvidiaApiKey",false)},
            {"openai","OpenAI compatible / custom","apiKey", llm.value("hasApiKey",false)},
        };
        for (const auto& p : provs) {
            const bool activo = providerActual == p.id;
            const bool expandido = proveedorExpandido_ == p.id;
            estado.items.push_back({{"kind","proveedor"},{"id",p.id},{"label",p.nombre},{"keyField",p.keyField},
                {"hasKey",p.hasKey},{"active",activo},{"expanded",expandido}});
            if (expandido) {
                estado.items.push_back({{"kind","prov-accion"},{"accion","apiKey"},{"provider",p.id},{"keyField",p.keyField},
                    {"label", p.hasKey ? "Cambiar API key" : "Cargar API key"}});
                if (p.hasKey) {
                    estado.items.push_back({{"kind","prov-accion"},{"accion","modelos"},{"provider",p.id},{"label","Ver/cargar modelos del proveedor"}});
                    estado.items.push_back({{"kind","prov-accion"},{"accion","thinking"},{"provider",p.id},{"label","Configurar pensamiento (off/low/medium/high/xhigh)"}});
                    estado.items.push_back({{"kind","prov-accion"},{"accion","power"},{"provider",p.id},{"label","Configurar calidad/power (low/medium/high/xhigh)"}});
                }
                // Mostrar modelos cacheados si corresponden a este provider
                if (p.hasKey && proveedorModelos_.provider == p.id) {
                    if (!proveedorModelos_.error.empty()) {
                        estado.items.push_back({{"kind","info"},{"label","Modelos"},{"value",proveedorModelos_.error}});
                    }
                    for (size_t i=0; i<proveedorModelos_.modelos.size() && i<60; ++i) {
                        const auto& m = proveedorModelos_.modelos[i];
                        estado.items.push_back({{"kind","prov-modelo"},{"provider",p.id},
                            {"id", m.value("id", std::string{})},
                            {"modelRef", m.value("modelRef", std::string{})},
                            {"reasoning", m.value("reasoning", false)},
                            {"variants", m.value("variants", nlohmann::json::array())}});
                    }
                }
            }
        }
        estado.items.push_back({{"kind","titulo"},{"label","Comandos rápidos"}});
        estado.items.push_back({{"kind","info"},{"label","Cambiar modelo"},{"value","cli.config.set modelRef=\"provider/modelo\""}});
        estado.items.push_back({{"kind","info"},{"label","Cambiar pensamiento"},{"value","cli.config.set thinking=medium (usa proveedor activo)"}});
    }
    estado.error.clear();
    const int tope = static_cast<int>(estado.items.size()) - 1;
    estado.seleccion = std::max(0, std::min(tope, seleccionPrevia));
    if (informar) {
        mensaje_ = esProveedores
            ? "Proveedores: Enter selecciona proveedor/modelo, cambia API key o pensamiento"
            : "Config: " + r.value("configPath", std::string{}) + " · Enter edita la ruta, el idioma o el color seleccionado";
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
    if (tipo == "ruta" || tipo == "llm") {
        const std::string clave = item.value("key", std::string{});
        if (item.value("secret", false)) {
            textoComando_ = "cli.config.set " + clave + "=\"\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Escribe la API key entre comillas y pulsa Enter · no se muestra la key guardada";
            mensajeEsError_ = false;
            return;
        }
        if (clave == "thinking") {
            textoComando_ = "cli.config.set thinking=\"medium\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Valores: off, low, medium, high, xhigh (si el proveedor lo soporta)";
            mensajeEsError_ = false;
            return;
        }
        if (clave == "power") {
            textoComando_ = "cli.config.set power=\"medium\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Valores: low, medium, high, xhigh";
            mensajeEsError_ = false;
            return;
        }
        if (clave == "modelName") {
            textoComando_ = "cli.config.set modelName=\"\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Escribe el id del modelo remoto; usa 'Listar modelos remotos' para ver opciones";
            mensajeEsError_ = false;
            return;
        }
        if (clave == "modelRef") {
            textoComando_ = "cli.config.set modelRef=\"\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Formato: provider/model o provider/model#variant (ej. google/gemini-2.5-pro#high)";
            mensajeEsError_ = false;
            return;
        }
        editarRutaConfiguracion(item);
    } else if (tipo == "llm-modelos-listar") {
        abrirSelectorModelos();
    } else if (tipo == "proveedor") {
        const std::string id = item.value("id", std::string{});
        proveedorExpandido_ = (proveedorExpandido_ == id) ? std::string{} : id;
        // Seleccionar proveedor activo al expandir; no pisa modelName si ya hay key.
        auto r = llamar("cli.config.set", nlohmann::json{{"provider", id}});
        if (r.respuesta.correcta) {
            auto cfg = agente::llm::LlmConfig::load();
            if (cfg.mode == agente::llm::LlmMode::REMOTE && cfg.validate().empty()) silvy_.configurarLlm(cfg, true);
        }
        mensaje_ = "Proveedor: " + id + (proveedorExpandido_.empty() ? " (plegado)" : " (expandido)");
        mensajeEsError_ = false;
        recargarSeccionActual(false);
    } else if (tipo == "prov-accion") {
        const std::string accion = item.value("accion", std::string{});
        const std::string prov = item.value("provider", std::string{});
        if (accion == "apiKey") {
            const std::string keyField = item.value("keyField", std::string{"apiKey"});
            textoComando_ = "cli.config.set provider=\"" + prov + "\" " + keyField + "=\"\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Pega la API key de " + prov + " y pulsa Enter";
            mensajeEsError_ = false;
        } else if (accion == "modelos") {
            cargarModelosProveedor(prov);
            recargarSeccionActual(false);
        } else if (accion == "thinking") {
            textoComando_ = "cli.config.set thinking=\"medium\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Pensamiento para proveedor activo (" + prov + "): off, low, medium, high, xhigh";
            mensajeEsError_ = false;
        } else if (accion == "power") {
            textoComando_ = "cli.config.set power=\"medium\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Calidad/power para proveedor activo: low, medium, high, xhigh";
            mensajeEsError_ = false;
        }
    } else if (tipo == "prov-modelo") {
        const std::string modelRef = item.value("modelRef", item.value("id", std::string{}));
        aplicarModeloProveedor(modelRef);
        if (item.value("reasoning", false)) {
            const auto vars = item.value("variants", nlohmann::json::array());
            std::string sugerido = "medium";
            if (vars.is_array() && !vars.empty() && vars[0].is_string()) sugerido = vars[0].get<std::string>();
            textoComando_ = "cli.config.set modelRef=\"" + modelRef + "#" + sugerido + "\"";
            cursorComando_ = static_cast<int>(textoComando_.size()) - 1;
            focoEnEntrada_ = true;
            mensaje_ = "Modelo con razonamiento: ajusta variant/thinking (off/low/medium/high/xhigh) y Enter";
            mensajeEsError_ = false;
        }
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
    if (tipo == "restablecer" || tipo == "docs-actualizar" || tipo == "llm-modelos-listar") {
        return ftxui::text("  " + recortar(etiqueta, ancho)) | ftxui::color(kTexto);
    }
    if (tipo == "proveedor") {
        const bool active = item.value("active", false);
        const bool expanded = item.value("expanded", false);
        const bool hasKey = item.value("hasKey", false);
        std::string marca = expanded ? "▾ " : "▸ ";
        std::string estado = active ? " [activo]" : "";
        estado += hasKey ? " [API key]" : " [sin API key]";
        return ftxui::hbox({
            ftxui::text("  " + marca + recortar(etiqueta, ancho > 40 ? ancho - 40 : 10)),
            ftxui::text(estado) | ftxui::color(active ? kExito : kTextoTenue),
        });
    }
    if (tipo == "prov-accion") {
        return ftxui::text("      • " + recortar(etiqueta, ancho > 8 ? ancho - 8 : ancho)) | ftxui::color(kAcento);
    }
    if (tipo == "prov-modelo") {
        const std::string id = item.value("id", std::string{});
        const bool razon = item.value("reasoning", false);
        const std::string suf = razon ? " [razonamiento]" : "";
        return ftxui::hbox({
            ftxui::text("      - " + recortar(id, ancho > 24 ? ancho - 24 : 8)),
            ftxui::text(suf) | ftxui::color(kTextoTenue),
        });
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
    // Ruta o ajuste de la CLI / LLM.
    const std::string valor = item.value("value", std::string{});
    const std::string efectiva = item.value("effective", std::string{});
    const bool secreto = item.value("secret", false);
    const bool porDefecto = valor.empty();
    const std::string mostrado = secreto ? (valor.empty() ? "no configurada" : "configurada")
                                         : (porDefecto ? efectiva : valor);
    const std::size_t anchoRuta = ancho > 44 ? ancho - 44 : 10;
    return ftxui::hbox({
        ftxui::text(columna("  " + etiqueta, 30)),
        ftxui::text(recortar(mostrado.empty() ? "(sin definir)" : mostrado, anchoRuta)) |
            ftxui::color(porDefecto ? kTextoTenue : kAcento),
        ftxui::filler(),
        ftxui::text(secreto ? (porDefecto ? " falta " : " guardada ")
                            : (porDefecto ? " por defecto " : " personalizada ")) |
            ftxui::color(porDefecto ? kTextoTenue : kExito),
    });
}

void AplicacionTui::abrirSelectorModelos() {
    auto res = llamar("cli.models.list", nlohmann::json::object());
    selectorModelos_ = SelectorModelos{};
    selectorModelos_.abierto = true;
    if (!res.respuesta.correcta) {
        selectorModelos_.error = res.respuesta.codigoError + ": " + res.respuesta.mensajeError;
        return;
    }
    selectorModelos_.provider = res.respuesta.resultado.value("provider", std::string{});
    const auto& modelos = res.respuesta.resultado.value("models", nlohmann::json::array());
    if (modelos.is_array()) for (const auto& m : modelos) selectorModelos_.modelos.push_back(m);
    if (selectorModelos_.modelos.empty()) selectorModelos_.error = "No se recibieron modelos del proveedor.";
}

void AplicacionTui::cargarModelosProveedor(const std::string& provider) {
    auto res = llamar("cli.models.list", nlohmann::json{{"provider", provider}});
    proveedorModelos_ = CacheModelosProveedor{};
    proveedorModelos_.provider = provider;
    if (!res.respuesta.correcta) {
        proveedorModelos_.error = res.respuesta.codigoError + ": " + res.respuesta.mensajeError;
        return;
    }
    const auto& modelos = res.respuesta.resultado.value("models", nlohmann::json::array());
    if (modelos.is_array()) for (const auto& m : modelos) proveedorModelos_.modelos.push_back(m);
    if (proveedorModelos_.modelos.empty()) proveedorModelos_.error = "No se recibieron modelos.";
}

void AplicacionTui::aplicarModeloProveedor(const std::string& modelRef) {
    auto r = llamar("cli.config.set", nlohmann::json{{"modelRef", modelRef}});
    if (r.respuesta.correcta) {
        auto cfg = agente::llm::LlmConfig::load();
        if (cfg.mode == agente::llm::LlmMode::REMOTE && cfg.validate().empty()) silvy_.configurarLlm(cfg, true);
        mensaje_ = "Modelo configurado: " + modelRef + " · Enter sobre el modelo para aplicar; usa cli.config.set thinking=... para razonamiento";
        mensajeEsError_ = false;
        recargarSeccionActual(false);
    } else {
        mensaje_ = r.respuesta.codigoError + ": " + r.respuesta.mensajeError;
        mensajeEsError_ = true;
    }
}

bool AplicacionTui::eventoSelectorModelos(ftxui::Event evento) {
    auto& s = selectorModelos_;
    if (evento == ftxui::Event::Escape) { s.abierto = false; return true; }
    if (evento == ftxui::Event::ArrowDown || evento == ftxui::Event::Character('j')) {
        if (!s.modelos.empty()) s.seleccion = std::min<int>(s.seleccion + 1, (int)s.modelos.size() - 1);
        return true;
    }
    if (evento == ftxui::Event::ArrowUp || evento == ftxui::Event::Character('k')) {
        if (!s.modelos.empty()) s.seleccion = std::max(0, s.seleccion - 1);
        return true;
    }
    if (evento == ftxui::Event::Return && !s.modelos.empty()) {
        const auto& m = s.modelos[s.seleccion];
        const std::string modelRef = m.value("modelRef", s.provider + "/" + m.value("id", std::string{}));
        auto r = llamar("cli.config.set", nlohmann::json{{"modelRef", modelRef}});
        if (r.respuesta.correcta) {
            auto cfg = agente::llm::LlmConfig::load();
            if (cfg.mode == agente::llm::LlmMode::REMOTE && cfg.validate().empty()) silvy_.configurarLlm(cfg, true);
            mensaje_ = "Modelo configurado: " + modelRef;
            mensajeEsError_ = false;
            s.abierto = false;
            recargarSeccionActual(false);
        } else {
            s.error = r.respuesta.codigoError + ": " + r.respuesta.mensajeError;
        }
        return true;
    }
    if (evento == ftxui::Event::Character('r')) {
        abrirSelectorModelos();
        return true;
    }
    return true;
}

ftxui::Element AplicacionTui::capaSelectorModelos() {
    using namespace ftxui;
    auto& s = selectorModelos_;
    if (!s.abierto) return emptyElement();
    Elements filas;
    filas.push_back(text(" Modelos remotos (" + (s.provider.empty()?"?":s.provider) + ") ") | bold | color(kAcento));
    filas.push_back(separator());
    if (!s.error.empty()) filas.push_back(paragraph("Error: " + s.error) | color(kError));
    int maxVis = 18;
    int inicio = 0;
    if (s.seleccion >= maxVis) inicio = s.seleccion - maxVis + 1;
    int fin = std::min<int>((int)s.modelos.size(), inicio + maxVis);
    for (int i = inicio; i < fin; ++i) {
        const auto& m = s.modelos[i];
        std::string id = m.value("id", std::string{});
        std::string mr = m.value("modelRef", id);
        bool razon = m.value("reasoning", false);
        std::string suf = razon ? " [reasoning]" : "";
        auto linea = text((i == s.seleccion ? "> " : "  ") + mr + suf);
        if (i == s.seleccion) linea = linea | inverted;
        filas.push_back(linea);
    }
    if (s.modelos.empty() && s.error.empty()) filas.push_back(text("Cargando/no hay modelos.") | color(kTextoTenue));
    filas.push_back(separator());
    filas.push_back(text("Enter aplica modelRef · r recargar · Esc cerrar") | color(kTextoTenue));
    return dbox({filler() | bgcolor(Color::RGBA(0,0,0,120)), center(vbox(std::move(filas)) | border | bgcolor(kFondoPanel) | size(WIDTH, LESS_THAN, 100) | size(HEIGHT, LESS_THAN, 28))});
}

}  // namespace funshi
