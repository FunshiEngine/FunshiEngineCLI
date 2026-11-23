/*
 * Copyright 2024 FunshiEngine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// HfApi: interpretación de las respuestas JSON de la API de Hugging Face.
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/HuggingFace/HfTypes.h"
#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <curl/curl.h>
#include <openssl/sha.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <vector>


namespace funshi::agente::llm {

namespace {

// Helper C++17 para ends_with
bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Extrae la cuantización del nombre de archivo GGUF: Q4_K_M, IQ2_XXS,
// Q4_0, Q8_0, BF16, F16... Busca el último token que encaje, separado por
// '-', '.', '_' o '/'. Devuelve "" si no hay ninguno.
std::string detectarCuantizacion(const std::string& nombreArchivo) {
    static const std::regex patron(
        R"((?:^|[-._/])((?:UD-)?(?:I?Q[1-8](?:_[0-9])?(?:_K)?(?:_(?:XXS|XS|S|M|L|XL|NL))?|BF16|F16|FP16|F32|FP32|MXFP4(?:_MOE)?|TQ[12]_0))(?=[-._]|$))",
        std::regex::icase);
    std::string mejor;
    for (auto it = std::sregex_iterator(nombreArchivo.begin(), nombreArchivo.end(), patron);
         it != std::sregex_iterator(); ++it) {
        mejor = (*it)[1].str();
    }
    std::transform(mejor.begin(), mejor.end(), mejor.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (mejor.rfind("UD-", 0) == 0) mejor = mejor.substr(3);
    if (mejor == "FP16") mejor = "F16";
    if (mejor == "FP32") mejor = "F32";
    return mejor;
}

// Lectores tolerantes: la API de HF mezcla tipos (gated: false | "auto",
// cardData: objeto | null, license: texto | lista). Un tipo inesperado no
// debe tumbar toda la búsqueda.
std::string textoCardData(const nlohmann::json& item, const char* clave) {
    auto it = item.find("cardData");
    if (it == item.end() || !it->is_object()) return {};
    auto v = it->find(clave);
    if (v == it->end()) return {};
    if (v->is_string()) return v->get<std::string>();
    if (v->is_array() && !v->empty() && (*v)[0].is_string()) return (*v)[0].get<std::string>();
    return {};
}

bool esGated(const nlohmann::json& item) {
    auto it = item.find("gated");
    if (it == item.end()) return false;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_string()) return !it->get<std::string>().empty() && it->get<std::string>() != "false";
    return false;
}

std::vector<std::string> listaTags(const nlohmann::json& item) {
    std::vector<std::string> tags;
    auto it = item.find("tags");
    if (it == item.end() || !it->is_array()) return tags;
    for (const auto& t : *it) if (t.is_string()) tags.push_back(t.get<std::string>());
    return tags;
}

long numeroDe(const nlohmann::json& item, const char* clave) {
    auto it = item.find(clave);
    return (it != item.end() && it->is_number()) ? it->get<long>() : 0L;
}

}  // namespace
SearchResult HfApi::parseSearchResponse(const nlohmann::json& json) {
    SearchResult result;
    if (!json.is_array()) return result;

    for (const auto& item : json) {
        ModelInfo model;
        model.id = item.value("modelId", item.value("id", ""));
        
        size_t slash = model.id.find('/');
        if (slash != std::string::npos) {
            model.author = model.id.substr(0, slash);
            model.name = model.id.substr(slash + 1);
        } else {
            model.author = "";
            model.name = model.id;
        }

        model.description = textoCardData(item, "description");
        model.pipelineTag = item.value("pipeline_tag", "");
        model.tags = listaTags(item);
        model.downloads = numeroDe(item, "downloads");
        model.likes = numeroDe(item, "likes");
        model.lastModified = item.value("lastModified", "");
        model.license = textoCardData(item, "license");
        model.gated = esGated(item);
        model.repoUrl = "https://huggingface.co/" + model.id;

        if (item.contains("siblings") && item["siblings"].is_array()) {
            for (const auto& sib : item["siblings"]) {
                if (!sib.contains("rfilename")) continue;
                std::string fname = sib["rfilename"].get<std::string>();
                if (endsWith(fname, ".gguf") || endsWith(fname, ".safetensors") || 
                    endsWith(fname, ".bin") || endsWith(fname, ".pt") || endsWith(fname, ".pth")) {
                    ModelVariant var;
                    var.filename = fname;
                    var.sizeBytes = sib.value("size", static_cast<std::uint64_t>(0));
                    if (var.sizeBytes == 0 && sib.contains("lfs") && sib["lfs"].is_object()) var.sizeBytes = sib["lfs"].value("size", static_cast<std::uint64_t>(0));
                    var.sizeGb = static_cast<double>(var.sizeBytes) / (1024.0 * 1024.0 * 1024.0);
                    var.downloadUrl = "https://huggingface.co/" + model.id + "/resolve/main/" + fname;
                    var.sha256 = sib.value("lfs", nlohmann::json::object()).value("sha256", "");
                    var.quantization = detectarCuantizacion(fname);
                    model.variants.push_back(std::move(var));
                }
            }
        }

        result.models.push_back(std::move(model));
    }

    result.total = static_cast<int>(result.models.size());
    result.pageSize = static_cast<int>(result.models.size());
    result.hasMore = result.models.size() >= static_cast<size_t>(result.pageSize);
    return result;
}

ModelInfo HfApi::parseModelInfo(const nlohmann::json& json) {
    ModelInfo model;
    model.id = json.value("modelId", json.value("id", ""));
    
    size_t slash = model.id.find('/');
    if (slash != std::string::npos) {
        model.author = model.id.substr(0, slash);
        model.name = model.id.substr(slash + 1);
    } else {
        model.author = "";
        model.name = model.id;
    }

    model.description = textoCardData(json, "description");
    model.pipelineTag = json.value("pipeline_tag", "");
    model.tags = listaTags(json);
    model.downloads = numeroDe(json, "downloads");
    model.likes = numeroDe(json, "likes");
    model.lastModified = json.value("lastModified", "");
    model.license = textoCardData(json, "license");
    model.gated = esGated(json);
    model.repoUrl = "https://huggingface.co/" + model.id;

    if (json.contains("siblings") && json["siblings"].is_array()) {
        for (const auto& sib : json["siblings"]) {
            if (!sib.contains("rfilename")) continue;
            std::string fname = sib["rfilename"].get<std::string>();
            if (endsWith(fname, ".gguf") || endsWith(fname, ".safetensors") || 
                endsWith(fname, ".bin") || endsWith(fname, ".pt") || endsWith(fname, ".pth")) {
                ModelVariant var;
                var.filename = fname;
                var.sizeBytes = sib.value("size", static_cast<std::uint64_t>(0));
                    if (var.sizeBytes == 0 && sib.contains("lfs") && sib["lfs"].is_object()) var.sizeBytes = sib["lfs"].value("size", static_cast<std::uint64_t>(0));
                    var.sizeGb = static_cast<double>(var.sizeBytes) / (1024.0 * 1024.0 * 1024.0);
                var.downloadUrl = "https://huggingface.co/" + model.id + "/resolve/main/" + fname;
                var.sha256 = sib.value("lfs", nlohmann::json::object()).value("sha256", "");
                var.quantization = detectarCuantizacion(fname);
                model.variants.push_back(std::move(var));
            }
        }
    }

    return model;
}

std::vector<ModelVariant> HfApi::parseVariants(const nlohmann::json& json, const std::string& modelId) {
    std::vector<ModelVariant> variants;
    if (json.contains("siblings") && json["siblings"].is_array()) {
        for (const auto& sib : json["siblings"]) {
            if (!sib.contains("rfilename")) continue;
            std::string fname = sib["rfilename"].get<std::string>();
            if (endsWith(fname, ".gguf") || endsWith(fname, ".safetensors") || 
                endsWith(fname, ".bin") || endsWith(fname, ".pt") || endsWith(fname, ".pth")) {
                ModelVariant var;
                var.filename = fname;
                var.sizeBytes = sib.value("size", static_cast<std::uint64_t>(0));
                    if (var.sizeBytes == 0 && sib.contains("lfs") && sib["lfs"].is_object()) var.sizeBytes = sib["lfs"].value("size", static_cast<std::uint64_t>(0));
                    var.sizeGb = static_cast<double>(var.sizeBytes) / (1024.0 * 1024.0 * 1024.0);
                var.downloadUrl = "https://huggingface.co/" + modelId + "/resolve/main/" + fname;
                var.sha256 = sib.value("lfs", nlohmann::json::object()).value("sha256", "");
                var.quantization = detectarCuantizacion(fname);
                variants.push_back(std::move(var));
            }
        }
    }
    return variants;
}

}  // namespace funshi::agente::llm
