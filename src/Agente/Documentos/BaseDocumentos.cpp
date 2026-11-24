/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Documentos/BaseDocumentos.h"

#include "Agente/Documentos/ExtractorTexto.h"
#include "Agente/LLM/Config/LlmConfig.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>

namespace funshi::agente::documentos {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kLote = 4;                 // fragmentos por llamada de embeddings
constexpr std::size_t kMaxContexto = 700;        // caracteres por fragmento en el contexto

std::int64_t mtimeDe(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
}

std::int64_t ahora() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string recortar(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t c = max;
    while (c > 0 && (static_cast<unsigned char>(s[c]) & 0xC0) == 0x80) --c;
    return s.substr(0, c) + "…";
}

}  // namespace

BaseDocumentos::BaseDocumentos(fs::path carpeta, fs::path archivoBase)
    : carpeta_(std::move(carpeta)), archivoBase_(std::move(archivoBase)) {}

BaseDocumentos& BaseDocumentos::global() {
    static BaseDocumentos* base = [] {
        const auto cfg = llm::LlmConfig::load();
        return new BaseDocumentos(cfg.documentosDirEfectivo(), llm::LlmConfig::configPath().parent_path() / "documentos.fvdb");
    }();
    return *base;
}

void BaseDocumentos::prepararCarpeta() const {
    std::error_code ec;
    fs::create_directories(carpeta_, ec);
    const fs::path leeme = carpeta_ / "LEEME.txt";
    if (fs::exists(leeme, ec)) return;
    std::ofstream out(leeme);
    out << "Documentos para Silvy\n"
           "=====================\n\n"
           "Deja aquí los archivos que Silvy debe poder consultar: PDF, Word (.docx), OpenDocument\n"
           "(.odt), PowerPoint (.pptx), Excel (.xlsx), EPUB, HTML, Markdown, texto y código.\n"
           "Al abrir la TUI se indexan los nuevos o cambiados; con /docs actualizar se indexan en el momento.\n"
           "Esta carpeta no se sube a git.\n";
}

std::vector<fs::path> BaseDocumentos::archivos() const {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(carpeta_, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (p.filename() == "LEEME.txt" || p.filename().string().rfind('.', 0) == 0) continue;
        if (esExtraible(p)) out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Fragmento> BaseDocumentos::fragmentar(const std::string& texto, std::size_t maximo, std::size_t solape) {
    std::vector<Fragmento> out;
    std::string actual;
    int pagina = 1, paginaActual = 1;
    const bool conPaginas = texto.find('\f') != std::string::npos;
    const auto cerrar = [&]() {
        if (actual.find_first_not_of(" \n\t\r") == std::string::npos) { actual.clear(); return; }
        out.push_back({actual, conPaginas ? paginaActual : 0, {}});
        // Solape: el final del fragmento anterior, desde un límite de palabra.
        std::string cola = actual.size() > solape ? actual.substr(actual.size() - solape) : std::string();
        const auto esp = cola.find(' ');
        actual = esp == std::string::npos ? std::string() : cola.substr(esp + 1);
        paginaActual = pagina;
    };
    std::size_t pos = 0;
    while (pos < texto.size()) {
        // Párrafo: hasta una línea en blanco o un cambio de página.
        std::size_t fin = texto.find("\n\n", pos);
        const std::size_t ff = texto.find('\f', pos);
        const bool porPagina = ff != std::string::npos && (fin == std::string::npos || ff < fin);
        if (porPagina) fin = ff;
        if (fin == std::string::npos) fin = texto.size();
        std::string parrafo = texto.substr(pos, fin - pos);
        pos = fin + (porPagina ? 1 : 2);
        // Párrafos más largos que el máximo: por palabras.
        while (!parrafo.empty()) {
            if (actual.size() + parrafo.size() + 1 <= maximo) {
                actual += (actual.empty() ? "" : "\n") + parrafo;
                parrafo.clear();
            } else if (actual.size() > solape + 50) {
                cerrar();
            } else {
                std::size_t corte = parrafo.rfind(' ', maximo - std::min(actual.size(), maximo - 1));
                if (corte == std::string::npos || corte == 0) corte = std::min(parrafo.size(), maximo);
                actual += (actual.empty() ? "" : " ") + parrafo.substr(0, corte);
                parrafo.erase(0, corte);
                cerrar();
            }
        }
        if (porPagina) {
            // Cada fragmento es de una sola página: se cierra en el cambio.
            cerrar();
            actual.clear();
            ++pagina;
            paginaActual = pagina;
        }
    }
    cerrar();
    return out;
}

void BaseDocumentos::cargarSiHaceFalta() const {
    if (cargado_) return;
    almacen_.cargar(archivoBase_);
    cargado_ = true;
    const_cast<BaseDocumentos*>(this)->reconstruirLexico();
}

void BaseDocumentos::reconstruirLexico() {
    lexico_ = conocimiento::IndiceBm25();
    mapaLexico_.clear();
    for (std::size_t d = 0; d < almacen_.documentos.size(); ++d) {
        const auto& doc = almacen_.documentos[d];
        for (std::size_t f = 0; f < doc.fragmentos.size(); ++f) {
            lexico_.agregar(doc.ruta + " " + doc.fragmentos[f].texto);
            mapaLexico_.push_back({d, f});
        }
    }
    lexico_.construir();
    estado_.archivos = static_cast<int>(almacen_.documentos.size());
    estado_.fragmentos = static_cast<int>(almacen_.totalFragmentos());
    estado_.conVector = static_cast<int>(almacen_.fragmentosConVector());
    estado_.conError = static_cast<int>(std::count_if(almacen_.documentos.begin(), almacen_.documentos.end(),
                                                      [](const DocumentoIndexado& x) { return !x.error.empty(); }));
    estado_.modelo = almacen_.modelo;
}

void BaseDocumentos::fijarEstado(const std::function<void(EstadoDocumentos&)>& cambio, const Progreso& progreso) {
    EstadoDocumentos copia;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cambio(estado_);
        copia = estado_;
    }
    if (progreso) progreso(copia);
}

EstadoDocumentos BaseDocumentos::estado() const {
    std::lock_guard<std::mutex> lock(mutex_);
    cargarSiHaceFalta();
    return estado_;
}

bool BaseDocumentos::actualizar(ProveedorEmbeddings* proveedor, const Progreso& progreso) {
    if (enCurso_.exchange(true)) return false;
    struct Fin { std::atomic<bool>& f; ~Fin() { f = false; } } fin{enCurso_};
    AlmacenVectores base;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cargarSiHaceFalta();
        base = almacen_;
    }
    fijarEstado([](EstadoDocumentos& e) { e.actualizando = true; e.fase = "revisando la carpeta"; e.ultimoError.clear(); }, progreso);

    const std::string modelo = proveedor ? proveedor->modelo() : base.modelo;
    const bool cambioModelo = proveedor && !base.modelo.empty() && base.modelo != modelo;
    AlmacenVectores nueva;
    nueva.modelo = modelo;
    nueva.dimension = cambioModelo ? 0 : base.dimension;
    const auto lista = archivos();
    std::string ultimoError;
    for (std::size_t i = 0; i < lista.size(); ++i) {
        const fs::path& archivo = lista[i];
        const std::string ruta = fs::relative(archivo, carpeta_).generic_string();
        std::error_code ec;
        const auto tamano = static_cast<std::uint64_t>(fs::file_size(archivo, ec));
        const auto mtime = mtimeDe(archivo);
        const DocumentoIndexado* previo = base.buscarDocumento(ruta);
        const bool faltanVectores = proveedor && previo &&
            std::any_of(previo->fragmentos.begin(), previo->fragmentos.end(), [](const Fragmento& f) { return f.vector.empty(); });
        if (previo && previo->mtime == mtime && previo->tamano == tamano && !cambioModelo && !faltanVectores) {
            nueva.documentos.push_back(*previo);  // sin cambios
            continue;
        }
        const std::string prefijo = "[" + std::to_string(i + 1) + "/" + std::to_string(lista.size()) + "] " + ruta;
        fijarEstado([&](EstadoDocumentos& e) { e.fase = prefijo + ": extrayendo texto"; }, progreso);
        DocumentoIndexado doc;
        doc.ruta = ruta;
        doc.mtime = mtime;
        doc.tamano = tamano;
        const TextoExtraido t = extraerTexto(archivo);
        doc.metodo = t.metodo;
        if (!t.ok) {
            doc.error = t.error;
        } else {
            doc.fragmentos = fragmentar(t.texto);
            if (proveedor) {
                for (std::size_t ini = 0; ini < doc.fragmentos.size(); ini += kLote) {
                    fijarEstado([&](EstadoDocumentos& e) {
                        e.fase = prefijo + ": embeddings " + std::to_string(ini) + "/" + std::to_string(doc.fragmentos.size());
                    }, progreso);
                    std::vector<std::string> textos;
                    for (std::size_t k = ini; k < std::min(ini + kLote, doc.fragmentos.size()); ++k) textos.push_back(doc.fragmentos[k].texto);
                    std::vector<std::vector<float>> vectores;
                    std::string error;
                    if (!proveedor->embeber(textos, false, vectores, error)) {
                        ultimoError = error;
                        break;  // el resto queda sin vector (búsqueda léxica)
                    }
                    for (std::size_t k = 0; k < vectores.size(); ++k) {
                        if (nueva.dimension == 0) nueva.dimension = static_cast<int>(vectores[k].size());
                        if (static_cast<int>(vectores[k].size()) == nueva.dimension) doc.fragmentos[ini + k].vector = std::move(vectores[k]);
                    }
                }
            }
        }
        nueva.documentos.push_back(std::move(doc));
    }
    // Los documentos que conservan vectores de otro modelo los pierden.
    if (cambioModelo) {
        for (auto& d : nueva.documentos) for (auto& f : d.fragmentos) if (static_cast<int>(f.vector.size()) != nueva.dimension) f.vector.clear();
    }
    nueva.guardar(archivoBase_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        almacen_ = std::move(nueva);
        reconstruirLexico();
        estado_.actualizando = false;
        estado_.fase = "listo";
        estado_.ultimoError = ultimoError;
        estado_.actualizado = ahora();
    }
    fijarEstado([](EstadoDocumentos&) {}, progreso);
    return true;
}

std::vector<ResultadoDocumento> BaseDocumentos::buscar(const std::string& consulta, std::size_t k,
                                                       ProveedorEmbeddings* proveedor) const {
    std::vector<float> vector;
    if (proveedor) {
        std::vector<std::vector<float>> v;
        std::string error;
        if (proveedor->embeber({consulta}, true, v, error) && !v.empty()) vector = std::move(v.front());
    }
    std::lock_guard<std::mutex> lock(mutex_);
    cargarSiHaceFalta();
    std::vector<ResultadoDocumento> out;
    if (!vector.empty() && almacen_.fragmentosConVector() > 0 && proveedor && proveedor->modelo() == almacen_.modelo) {
        for (const auto& c : almacen_.masParecidos(vector, k)) {
            out.push_back({c.documento->ruta, c.fragmento->pagina, c.fragmento->texto, c.puntaje, true});
        }
        return out;
    }
    for (const auto& [i, p] : lexico_.buscar(consulta, k)) {
        const auto [d, f] = mapaLexico_[i];
        const auto& doc = almacen_.documentos[d];
        out.push_back({doc.ruta, doc.fragmentos[f].pagina, doc.fragmentos[f].texto, p, false});
    }
    return out;
}

std::string BaseDocumentos::contextoParaPedido(const std::string& pedido, ProveedorEmbeddings* proveedor,
                                               std::vector<std::string>* usados) const {
    const auto resultados = buscar(pedido, 3, proveedor);
    std::string out;
    for (const auto& r : resultados) {
        if (r.puntaje < (r.vectorial ? kUmbralVector : kUmbralLexico)) continue;
        out += "\n### " + r.ruta + (r.pagina > 0 ? " (página " + std::to_string(r.pagina) + ")" : "") + "\n" +
               recortar(r.texto, kMaxContexto) + "\n";
        if (usados) usados->push_back(r.ruta + (r.pagina > 0 ? " p." + std::to_string(r.pagina) : ""));
    }
    if (out.empty()) return {};
    return "[Fragmentos de los documentos del usuario relacionados con el pedido. Úsalos si sirven y "
           "di de qué archivo sale la información; para buscar más usa buscar_documentos.]\n" + out;
}

}  // namespace funshi::agente::documentos
