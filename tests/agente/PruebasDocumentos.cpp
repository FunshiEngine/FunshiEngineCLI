/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: RAG de documentos — extracción de texto (texto, HTML, DOCX,
// PDF propio), fragmentos, base FVDB, sincronización incremental con un
// proveedor de embeddings falso, búsqueda léxica y registros de servidor.
#include "PruebasAgente.h"

#include "Agente/Documentos/AlmacenVectores.h"
#include "Agente/Documentos/BaseDocumentos.h"
#include "Agente/Documentos/ExtractorTexto.h"
#include "Agente/LLM/Local/RegistroServidor.h"

#include <miniz/miniz.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

namespace pruebas_agente {

namespace {

namespace fs = std::filesystem;
namespace docs = funshi::agente::documentos;

void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

// Vector por palabras clave fijas: dos textos sobre lo mismo quedan cerca.
class EmbeddingsFalsos : public docs::ProveedorEmbeddings {
public:
    explicit EmbeddingsFalsos(std::string modelo) : modelo_(std::move(modelo)) {}
    std::string modelo() const override { return modelo_; }
    int llamadas = 0;
    bool embeber(const std::vector<std::string>& textos, bool, std::vector<std::vector<float>>& vectores,
                 std::string&) override {
        ++llamadas;
        static const char* claves[] = {"dragón", "espada", "nivel", "jefe", "salto", "agua", "fuego", "menú"};
        vectores.clear();
        for (const auto& t : textos) {
            std::vector<float> v;
            for (const char* c : claves) v.push_back(t.find(c) != std::string::npos ? 1.0f : 0.0f);
            v.push_back(0.05f);
            docs::normalizar(v);
            vectores.push_back(std::move(v));
        }
        return true;
    }

private:
    std::string modelo_;
};

// PDF mínimo con el contenido comprimido (FlateDecode), como los reales.
std::string pdfDePrueba(const std::string& texto) {
    const std::string contenido = "BT /F1 12 Tf 72 712 Td (" + texto + ") Tj ET";
    std::size_t tam = 0;
    void* comprimido = tdefl_compress_mem_to_heap(contenido.data(), contenido.size(), &tam, TDEFL_WRITE_ZLIB_HEADER);
    std::string flujo(static_cast<const char*>(comprimido), tam);
    mz_free(comprimido);
    return "%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
           "4 0 obj << /Length " + std::to_string(flujo.size()) + " /Filter /FlateDecode >>\nstream\n" + flujo +
           "\nendstream\nendobj\ntrailer << /Root 1 0 R >>\n%%EOF\n";
}

void docxDePrueba(const fs::path& destino, const std::string& texto) {
    mz_zip_archive zip{};
    mz_zip_writer_init_file(&zip, destino.string().c_str(), 0);
    const std::string xml = "<w:document><w:body><w:p><w:r><w:t>" + texto +
                            "</w:t></w:r></w:p><w:p><w:r><w:t>Segundo &amp; párrafo</w:t></w:r></w:p></w:body></w:document>";
    mz_zip_writer_add_mem(&zip, "word/document.xml", xml.data(), xml.size(), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
}

}  // namespace

void probarDocumentos() {
    const fs::path raiz = fs::current_path() / "docs-prueba";
    const fs::path carpeta = raiz / "documentos";
    fs::remove_all(raiz);

    // Extracción.
    escribir(carpeta / "notas.md", "# Diseño\nEl dragón aparece en el nivel 3.");
    escribir(carpeta / "pagina.html", "<html><head><style>p{}</style><script>x()</script></head><body><p>El jefe usa fuego</p><p>A&amp;B</p></body></html>");
    docxDePrueba(carpeta / "reglas.docx", "La espada rompe escudos");
    escribir(carpeta / "manual.pdf", pdfDePrueba("El salto doble se desbloquea en el agua"));
    escribir(carpeta / "imagen.png", std::string("\x89PNG\r\n\x1a\n\0\0", 10));
    escribir(carpeta / "binario.txt", std::string("abc\0def", 7));

    CHECK(docs::extraerTexto(carpeta / "notas.md").texto.find("dragón") != std::string::npos, "extracción: markdown");
    const auto html = docs::extraerTexto(carpeta / "pagina.html");
    CHECK(html.ok && html.texto.find("El jefe usa fuego\nA&B") != std::string::npos && html.texto.find("x()") == std::string::npos,
          "extracción: HTML sin scripts, con entidades (" + html.texto + ")");
    const auto docx = docs::extraerTexto(carpeta / "reglas.docx");
    CHECK(docx.ok && docx.metodo == "docx" && docx.texto.find("La espada rompe escudos\nSegundo & párrafo") != std::string::npos,
          "extracción: DOCX (" + docx.texto + ")");
    std::string bytesPdf;
    { std::ifstream in(carpeta / "manual.pdf", std::ios::binary); bytesPdf.assign(std::istreambuf_iterator<char>(in), {}); }
    CHECK(docs::textoDePdf(bytesPdf).find("El salto doble se desbloquea en el agua") != std::string::npos,
          "extracción: PDF con el extractor propio (FlateDecode)");
    CHECK(docs::extraerTexto(carpeta / "manual.pdf").ok, "extracción: PDF (pdftotext o propio)");
    CHECK(!docs::esExtraible(carpeta / "imagen.png"), "extracción: una imagen no se indexa");
    CHECK(!docs::extraerTexto(carpeta / "binario.txt").ok, "extracción: un binario disfrazado se descarta");
    CHECK(docs::aUtf8("caf\xe9") == "café", "extracción: Latin-1 a UTF-8");

    // Fragmentos con páginas.
    std::string largo;
    for (int i = 0; i < 40; ++i) largo += "Párrafo número " + std::to_string(i) + " con texto de relleno para el fragmento.\n\n";
    const auto frags = docs::BaseDocumentos::fragmentar(largo + "\fPágina dos.", 300, 60);
    CHECK(frags.size() > 5 && std::all_of(frags.begin(), frags.end(), [](const docs::Fragmento& f) { return f.texto.size() <= 360; }),
          "fragmentos: respetan el máximo");
    CHECK(frags.back().pagina == 2 && frags.front().pagina == 1, "fragmentos: llevan su página");

    // Base FVDB: ida y vuelta.
    docs::AlmacenVectores a;
    a.modelo = "m";
    a.dimension = 3;
    a.documentos.push_back({"x.txt", 5, 10, "texto", "", {{"hola", 1, {0.6f, 0.8f, 0.0f}}, {"sin vector", 0, {}}}});
    CHECK(a.guardar(raiz / "base.fvdb"), "fvdb: se guarda");
    docs::AlmacenVectores b;
    CHECK(b.cargar(raiz / "base.fvdb") && b.modelo == "m" && b.documentos.size() == 1 &&
              b.documentos[0].fragmentos[0].vector[1] == 0.8f && b.documentos[0].fragmentos[1].vector.empty(),
          "fvdb: se lee igual");
    escribir(raiz / "mala.fvdb", "XXXX");
    CHECK(!b.cargar(raiz / "mala.fvdb"), "fvdb: rechaza un archivo que no es FVDB");

    // Sincronización incremental con embeddings falsos.
    docs::BaseDocumentos base(carpeta, raiz / "documentos.fvdb");
    EmbeddingsFalsos emb("falso-1");
    CHECK(base.actualizar(&emb), "base: primera indexación");
    auto e = base.estado();
    CHECK(e.archivos == 5 && e.conError == 1 && e.fragmentos >= 4 && e.conVector == e.fragmentos && e.modelo == "falso-1",
          "base: 4 documentos con texto (y el binario con error) (" + std::to_string(e.archivos) + "/" + std::to_string(e.conError) + ")");
    const auto r = base.buscar("¿dónde aparece el dragón?", 2, &emb);
    CHECK(!r.empty() && r.front().vectorial && r.front().ruta == "notas.md", "base: búsqueda semántica");
    CHECK(base.contextoParaPedido("qué hace la espada", &emb).find("reglas.docx") != std::string::npos,
          "base: contexto automático con el archivo de origen");
    CHECK(base.contextoParaPedido("cómo compilo el proyecto", &emb).empty(), "base: sin contexto si nada se parece");

    const int antes = emb.llamadas;
    CHECK(base.actualizar(&emb) && emb.llamadas == antes, "base: sin cambios no vuelve a embeber");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    escribir(carpeta / "notas.md", "El menú tiene tres opciones.");
    fs::remove(carpeta / "pagina.html");
    base.actualizar(&emb);
    e = base.estado();
    CHECK(emb.llamadas == antes + 1 && e.archivos == 4, "base: re-embebe solo lo cambiado y quita lo borrado");

    EmbeddingsFalsos otro("falso-2");
    base.actualizar(&otro);
    CHECK(base.estado().modelo == "falso-2" && otro.llamadas >= 3, "base: con otro modelo se re-embebe todo");

    docs::BaseDocumentos lexica(carpeta, raiz / "lexica.fvdb");
    lexica.actualizar(nullptr);
    const auto rl = lexica.buscar("espada escudos", 1, nullptr);
    CHECK(lexica.estado().conVector == 0 && !rl.empty() && !rl.front().vectorial && rl.front().ruta == "reglas.docx",
          "base: sin embeddings busca por palabras");

    // Registros de servidor por nombre (chat y embeddings conviven).
    namespace llm2 = funshi::agente::llm;
    llm2::registrarServidor(raiz, 999999, 1, llm2::kRegistroChat);
    llm2::registrarServidor(raiz, 999998, 2, llm2::kRegistroEmbeddings);
    CHECK(fs::exists(raiz / "llama-server.pid") && fs::exists(raiz / "embeddings.pid"), "registro: un archivo por servidor");
    llm2::detenerServidorRegistrado(raiz, llm2::kRegistroEmbeddings);
    CHECK(fs::exists(raiz / "llama-server.pid") && !fs::exists(raiz / "embeddings.pid"), "registro: se detiene solo el pedido");
    llm2::detenerServidorRegistrado(raiz);
    fs::remove_all(raiz);
}

}  // namespace pruebas_agente
